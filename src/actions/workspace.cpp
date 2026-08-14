/*
 * Workspace-related actions: view/move dispatchers, special overlay toggles,
 * and helper flows for workspace switch side effects.
 *
 * Public `view_workspace_*` / `movetoworkspace_*` resolve
 * `wm::state::monitor_or_fallback(state)` at entry; `apply_normal_workspace_view` is the
 * monitor-scoped transition helper (registry routing, EWMH). `update_selected_monitor_bridge`
 * mutates `runtime_state->monitors.current` when non-null.
 */
#include "actions/workspace.hpp"

#include "bsp/add_flow.hpp"
#include "client/client_focus.hpp"
#include "client/client_props.hpp"
#include "config.hpp"
#include "config/parse/general.hpp"
#include "intern.hpp"
#include "monitor/world_state.hpp"
#include "context/monitor_context.hpp"
#include "monitor_select.hpp"
#include "special_workspace_registry.hpp"
#include "state/runtime_authority.hpp"
#include "state/wm_state_root.hpp"
#include "wm_state.hpp"
#include "x11/atoms.hpp"
#include "x11/connection.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_window.hpp"
#include "workspace_registry.hpp"
#include "sys/spawn.hpp"

#include <optional>
#include <string>

namespace {
    /* Register special tag on demand from action paths. */
    [[nodiscard]] bool ensure_special_tag_exists(std::string_view tag) {
        return special_workspace_registry_ensure_tag(std::string(tag)).has_value();
    }

    /* Spawn detached shell command for workspace hooks via shared sys helper. */
    void spawn_detached_shell(const std::string& cmd) noexcept {
        wm::sys::spawn_detached_shell(wm::x11::connection(), cmd);
    }
} // namespace

/* Run per-workspace "on-created-empty" command when armed and still empty.
 *
 * Execution model:
 * - Arm/disarm state is tracked in registry metadata.
 * - Spawns detached child (`setsid`) and executes `/bin/sh -c <cmd>`.
 * - Parent never blocks and does not throw.
 *
 * Safety:
 * - Re-checks emptiness before spawn to avoid stale-arm race in action flow.
 */
static void maybe_spawn_workspace_on_created_empty(WorkspaceId id) {
    WorkspaceMeta* meta = workspace_registry_meta_mut(id);
    if (!meta || meta->on_created_empty_cmd.empty() || !meta->on_empty_spawn_pending) [[unlikely]]
        return;
    if (count_clients_on_workspace(id) != 0) [[unlikely]]
        return;
    meta->on_empty_spawn_pending = false;
    spawn_detached_shell(meta->on_created_empty_cmd);
}

/* Follow-mouse post-view focus adjustment.
 *
 * Enabled only when strict focus-follows-pointer mode is active.
 * After a workspace view transition, this may focus the window currently under
 * pointer, but only if it is visible and focusable.
 */
static void apply_follow_mouse_focus_after_view(void) {
    Client* c;

    if (g_config.wm_input.follow_mouse != 1 || !wm::x11::connection())
        return;
    const auto ptr = wm::x11::query_pointer(wm::x11::root_window());
    if (!ptr || !ptr->same_screen || ptr->child == None)
        return;
    c = wintoclient(ptr->child);
    if (!client_is_visible(c) || c->neverfocus)
        return;
    focus(c);
}

/* Writes selected monitor into runtime authority; uses `runtime_state` view when caller holds one. */
static inline void update_selected_monitor_bridge(Monitor* m, wm::state::WMState* runtime_state) noexcept {
    if (!m) [[unlikely]]
        return;
    if (runtime_state) {
        runtime_state->monitors.current = m;
        return;
    }
    wm::state::runtime_authority().ref_current_monitor() = m;
}

static void wm_focus_after_special_overlay_hidden_impl(Monitor* m, wm::state::WMState* runtime_state) noexcept;
static void wm_focus_first_special_overlay_client_impl(Monitor* m, wm::state::WMState* runtime_state) noexcept;

void        wm_ensure_special_overlay_visible(Monitor* m, std::string_view tag, bool focus_first_client) {
    if (!m) [[unlikely]]
        return;
    if (!ensure_special_tag_exists(tag)) [[unlikely]]
        return;
    m->special_overlay_open = true;
    m->special_overlay_tag.assign(tag);
    /* Always honor focus_first_client (spawn/reopen must restore NetActiveWindow even when overlay flag already set). */
    const bool do_focus_pick = focus_first_client;
    arrange(m, !do_focus_pick);
    if (do_focus_pick)
        wm_focus_first_special_overlay_client_impl(m, nullptr);
}

/* Scan monitor client ring for any non-dock client mapped to workspace ref.
 *
 * Used by special-overlay toggle policy to decide hide vs keep-open behavior.
 */
[[nodiscard]] static bool monitor_has_any_client_on_workspace_ref(const Monitor* m, const WorkspaceRef& ws) noexcept {
    if (!m) [[unlikely]]
        return false;
    for (Client* c : m->clients) {
        if (c->isdock)
            continue;
        if (client_tree_member_on_workspace(c, m, ws))
            return true;
    }
    return false;
}

/* View/toggle policy for special workspace overlays.
 *
 * Behavior matrix:
 * - Overlay closed or different tag  -> open/switch and run overlay focus pick.
 * - Overlay already on tag + no-hide policy -> keep overlay, refresh focus policy.
 * - Overlay already on tag + allow-hide:
 *   - hide when no clients on tag OR selected client belongs to this overlay,
 *   - otherwise keep open and refresh overlay focus pick.
 */
static void view_special_workspace(Monitor* m, const WorkspaceRef& ws, bool allow_toggle_hide, wm::state::WMState* runtime_state = nullptr) {
    if (!m || !ws.is_special()) [[unlikely]]
        return;

    const std::string_view tag(ws.special_tag);
    if (!ensure_special_tag_exists(tag)) [[unlikely]]
        return;

    // Case 1: overlay not currently showing this tag -> open/switch and focus.
    if (!monitor_special_overlay_shows_tag(m, tag)) {
        update_selected_monitor_bridge(m, runtime_state);
        wm_ensure_special_overlay_visible(m, tag, true);
        return;
    }

    // Case 2: toggling disabled -> keep open and refresh focus policy only.
    if (!allow_toggle_hide) {
        wm_focus_first_special_overlay_client_impl(m, runtime_state);
        return;
    }

    // Case 3: toggle policy while same tag is open.
    const bool    has_any     = monitor_has_any_client_on_workspace_ref(m, ws);
    Client* const sel         = m->sel;
    const bool    sel_on_this = sel && !sel->neverfocus && client_tree_member_on_workspace(sel, m, ws) && client_is_visible_on_monitor(sel, m);
    if (!has_any || sel_on_this) {
        m->special_overlay_open = false;
        m->special_overlay_tag.clear();
        arrange(m, false);
        wm_focus_after_special_overlay_hidden_impl(m, runtime_state);
        return;
    }
    wm_focus_first_special_overlay_client_impl(m, runtime_state);
}

/* Resolve monitor target for workspace view using optional workspace monitor rule.
 *
 * Fallback:
 * - Returns current selected monitor when no monitor rule is present or resolution fails.
 */
[[nodiscard]] static Monitor* resolve_target_monitor_for_workspace(WorkspaceId id, Monitor* fallback_current) {
    const WorkspaceMeta wm = workspace_registry_effective_meta(id, fallback_current);
    if (!wm.rule_monitor_index.empty())
        if (Monitor* resolved = monitor_select_resolve(wm.rule_monitor_index))
            return resolved;
    return fallback_current;
}

/* Apply workspace-specific layout override when configured.
 *
 * Rule source:
 * - `workspace_registry_find_by_id(id)->rule_layout_name`
 *
 * Behavior:
 * - Only updates layout slot 0 and visible symbol when active slot is 0.
 * - Silently no-ops when layout name is empty/unknown.
 */
static void apply_workspace_layout_rule(Monitor* m, WorkspaceId id) {
    if (!m)
        return;

    const WorkspaceMeta wm = workspace_registry_effective_meta(id, m);
    if (wm.rule_layout_name.empty())
        return;

    if (void (*layout_fn)(Monitor*) = wm::config::parse::general::try_layout_by_name(wm.rule_layout_name); layout_fn) {
        for (std::size_t i = 0; i < g_config.layouts.size(); ++i) {
            if (g_config.layouts[i].arrange == layout_fn) {
                m->layout_slots[0] = &g_config.layouts[i];
                if (m->active_layout_slot == 0)
                    m->layout_label = g_config.layouts[i].symbol;
                break;
            }
        }
    }
}

/* Apply normal-workspace view transition for `workspace_id` starting from resolved monitor `current`. */
static bool apply_normal_workspace_view(wm::state::WMState& state, Monitor* current, WorkspaceId workspace_id) {
    if (!current)
        return false;
    Monitor* target = resolve_target_monitor_for_workspace(workspace_id, current);
    if (!target)
        return false;

    if (target == current && current->active_workspace_id == workspace_id)
        return false;

    [[maybe_unused]] const bool monitor_switched = switch_selected_monitor(state.monitors.current, target, true);
    current                                      = target;
    /* Normal workspace switch must close special overlay on that monitor. */
    if (current->special_overlay_open) {
        current->special_overlay_open = false;
        current->special_overlay_tag.clear();
    }

    monitor_set_active_workspace_id(current, workspace_id);
    MonitorWorldState(*current).sync_viewed_from_active_workspace();
    apply_workspace_layout_rule(current, workspace_id);

    focus(nullptr);
    arrange(current);
    apply_follow_mouse_focus_after_view();
    update_net_desktop_props();
    maybe_spawn_workspace_on_created_empty(workspace_id);
    return true;
}

void view_workspace_ref(wm::state::WMState& state, const WorkspaceRef& r) {
    Monitor* const     current    = wm::state::monitor_or_fallback(state);
    const WorkspaceRef target_ref = workspace_normalize_special_ref_with_hidden_id(r);
    if (target_ref.is_special()) {
        view_special_workspace(current, target_ref, true, &state);
        return;
    }
    if (target_ref.is_normal() && target_ref.normal_id >= kWorkspaceIdMin)
        static_cast<void>(apply_normal_workspace_view(state, current, target_ref.normal_id));
}

bool view_workspace_id(wm::state::WMState& state, WorkspaceId workspace_id) {
    return apply_normal_workspace_view(state, wm::state::monitor_or_fallback(state), workspace_id);
}

/* Common post-move refresh path for move actions that do not trigger view transition.
 *
 * Ensures focus tree, arrange state, and desktop exports are consistent after ownership
 * updates when the current viewed workspace remains unchanged.
 */
static inline void refresh_after_move(Monitor* m, bool needs_arrange) noexcept {
    focus(nullptr);
    if (needs_arrange)
        arrange(m);
    update_net_desktop_props();
}

/* Move selected client to workspace target with optional silent semantics.
 *
 * Non-silent mode:
 * - May switch view/overlay and apply resulting focus policy.
 *
 * Silent mode:
 * - Keeps current view while still updating ownership/tree state and exports.
 *
 * Additional side effects:
 * - Applies workspace border policy.
 * - Maintains empty-workspace hook arm/disarm state.
 * - Handles special-overlay autohide/focus recovery when source overlay empties.
 */
void movetoworkspace_ref(wm::state::WMState& state, const WorkspaceRef& w, bool silent) {
    Monitor* const     current    = wm::state::monitor_or_fallback(state);
    const WorkspaceRef target_ref = workspace_normalize_special_ref_with_hidden_id(w);
    if (!current) [[unlikely]]
        return;
    Client* c = current->sel;
    if (!c) [[unlikely]]
        return;

    if (target_ref.is_special()) {
        if (!ensure_special_tag_exists(target_ref.special_tag)) [[unlikely]]
            return;
    }

    if (c->workspace == target_ref) {
        if (!silent) {
            if (target_ref.is_special())
                wm_ensure_special_overlay_visible(current, target_ref.special_tag);
            else if (target_ref.is_normal()) {
                if (!apply_normal_workspace_view(state, current, target_ref.normal_id)) {
                    refresh_after_move(current, true);
                }
            }
        } else {
            refresh_after_move(current, true);
        }
        return;
    }

    const WorkspaceId old_ws = client_workspace_normal_id(c);

    bsp_remove_client(c);
    c->workspace = target_ref;
    setclientworkspaceprop(c);
    bsp_add_client(c, current);
    apply_client_workspace_border_policy(c);
    const bool special_hid = wm_special_overlay_autohide_if_empty(current);
    if (old_ws >= kWorkspaceIdMin && count_clients_on_workspace(old_ws) == 0)
        workspace_registry_note_workspace_became_empty(old_ws);
    if (target_ref.is_normal())
        workspace_registry_note_client_joined_workspace(target_ref.normal_id);

    if (!silent) {
        if (target_ref.is_special()) {
            update_selected_monitor_bridge(current, &state);
            wm_ensure_special_overlay_visible(current, target_ref.special_tag);
            return;
        }
        const bool viewed = apply_normal_workspace_view(state, current, target_ref.normal_id);
        if (!viewed) {
            refresh_after_move(current, true);
        }
        if (special_hid) {
            wm_focus_after_special_overlay_hidden_impl(current, &state);
        }
        return;
    }
    refresh_after_move(current, true);
    if (special_hid) {
        wm_focus_after_special_overlay_hidden_impl(current, &state);
    }
}

void movetoworkspace_id(wm::state::WMState& state, WorkspaceId workspace_id, bool silent) {
    movetoworkspace_ref(state, WorkspaceRef::normal(workspace_id), silent);
}

bool wm_special_overlay_autohide_if_empty(Monitor* m) {
    if (!m || !m->special_overlay_open || m->special_overlay_tag.empty()) [[unlikely]]
        return false;
    for (Client* c : m->clients) {
        if (c->isdock)
            continue;
        if (client_on_open_special_overlay_tag(c, m))
            return false;
    }
    m->special_overlay_open = false;
    m->special_overlay_tag.clear();
    return true;
}

/* Pointer-hit client on monitor `m` that passes `pred`, or nullptr. Shared by overlay open/hide focus. */
template <typename Pred>
[[nodiscard]] static Client* pointer_hit_client_on_monitor(Monitor* m, Pred pred) noexcept {
    if (!m || !wm::x11::connection()) [[unlikely]]
        return nullptr;
    const auto ptr = wm::x11::query_pointer(wm::x11::root_window());
    if (!ptr || !ptr->same_screen || ptr->child == None)
        return nullptr;
    Client* hit = wintoclient(ptr->child);
    if (!hit || hit->mon != m || !pred(hit))
        return nullptr;
    return hit;
}

static void wm_focus_after_special_overlay_hidden_impl(Monitor* m, wm::state::WMState* runtime_state) noexcept {
    if (!m) [[unlikely]]
        return;
    update_selected_monitor_bridge(m, runtime_state);
    if (Client* hit = pointer_hit_client_on_monitor(m, [](Client* c) noexcept { return client_is_visible(c) && !c->neverfocus; })) {
        focus(hit);
        restack(m);
        return;
    }
    focus(nullptr);
    restack(m);
}

void wm_focus_after_special_overlay_hidden(Monitor* m) noexcept {
    wm_focus_after_special_overlay_hidden_impl(m, nullptr);
}

static void wm_focus_first_special_overlay_client_impl(Monitor* m, wm::state::WMState* runtime_state) noexcept {
    if (!m || !m->special_overlay_open || m->special_overlay_tag.empty()) [[unlikely]]
        return;
    auto on_overlay_visible = [m](Client* c) noexcept {
        return c && !c->isdock && !c->neverfocus && client_on_open_special_overlay_tag(c, m) && client_is_visible_on_monitor(c, m);
    };
    /* Prefer client under pointer when it belongs to this overlay. */
    if (Client* hit = pointer_hit_client_on_monitor(m, on_overlay_visible)) {
        update_selected_monitor_bridge(m, runtime_state);
        focus(hit);
        restack(m);
        return;
    }
    /* Keep last focused client when reopening/toggling same special overlay. */
    if (on_overlay_visible(m->sel)) {
        update_selected_monitor_bridge(m, runtime_state);
        focus(m->sel); // sel may be set without NetActiveWindow after overlay hide/reopen
        restack(m);
        return;
    }
    Client* first = nullptr;
    for (Client* c : m->clients) {
        if (!on_overlay_visible(c))
            continue;
        /* Prefer active tab inside grouped leaves to preserve per-group focus semantics. */
        if (c->leaf && c->leaf->type == NODE_GROUPED && c->leaf->grouped.active < c->leaf->grouped.clients.size()) {
            Client* active = c->leaf->grouped.clients[c->leaf->grouped.active];
            if (on_overlay_visible(active)) {
                first = active;
                break;
            }
        }
        first = c;
        break;
    }
    update_selected_monitor_bridge(m, runtime_state);
    if (!first) {
        focus(nullptr);
        restack(m);
        return;
    }
    /* arrange(m,false) skipped restack; still apply X focus when sel matches but NetActiveWindow may differ. */
    if (m->sel == first) {
        focus(first);
        restack(m);
        return;
    }
    focus(first);
    restack(m);
}

void wm_focus_first_special_overlay_client(Monitor* m) noexcept {
    wm_focus_first_special_overlay_client_impl(m, nullptr);
}

namespace {
    /* Set or clear `_NET_WM_WINDOW_OPACITY` on a window (special overlay dim layer). */
    void x11_net_window_opacity(Window win, double opv) {
        xcb_connection_t* const conn = wm::x11::connection();
        if (!conn || !win)
            return;
        if (opv > 0.0 && opv < 1.0) {
            const uint32_t v = static_cast<uint32_t>(opv * static_cast<double>(0xffffffffu));
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, static_cast<xcb_window_t>(win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMWindowsOpacity)), XCB_ATOM_CARDINAL, 32,
                                1, &v);
        } else
            xcb_delete_property(conn, static_cast<xcb_window_t>(win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetWMWindowsOpacity)));
    }
} // namespace

/* Create/resize/hide the semi-transparent dim window behind an open special overlay. */
void update_special_dimwin(Monitor* m) {
    if (!m || !wm::x11::connection())
        return;
    if (!m->special_overlay_open) {
        if (m->special_dimwin) {
            wm::x11::unmap_window(m->special_dimwin);
            wm::x11::move_window(m->special_dimwin, -20000, 0);
        }
        return;
    }
    double dim_strength = g_config.dim_special;
    if (const SpecialWorkspaceMeta* sm = special_workspace_registry_find_by_tag(m->special_overlay_tag); sm && sm->rule_dim_special.has_value())
        dim_strength = sm->rule_dim_special.value();
    if (dim_strength <= 0.0) {
        if (m->special_dimwin) {
            wm::x11::unmap_window(m->special_dimwin);
            wm::x11::move_window(m->special_dimwin, -20000, 0);
        }
        return;
    }
    wm::x11::WindowAttrs wa{};
    wa.override_redirect = true;
    wa.background_pixel  = 0;
    wa.event_mask        = 0;
    if (!m->special_dimwin) {
        if (!root_visual)
            return;
        m->special_dimwin =
            wm::x11::create_window(wm::x11::root_window(), 0, 0, 1, 1, 0, root_depth, CopyFromParent, root_visual->visual_id, CWOverrideRedirect | CWBackPixel | CWEventMask, wa);
        if (!m->special_dimwin)
            return;
    }
    /* Cover the full monitor workarea. Insetting by outer gaps left a ring of normal
     * desktop windows visible around the scratchpad (common "background pieces" glitch). */
    int rw = m->ww;
    int rh = m->wh;
    if (rw < 1)
        rw = 1;
    if (rh < 1)
        rh = 1;
    wm::x11::move_resize_window(m->special_dimwin, m->wx, m->wy, static_cast<unsigned>(rw), static_cast<unsigned>(rh));
    x11_net_window_opacity(m->special_dimwin, dim_strength);
    wm::x11::map_window(m->special_dimwin);
}
