/*
 * Key/button command implementations declared in actions.hpp.
 * Uses WM state (selected monitor, monitor chain, xcb, g_config.layouts, g_config.wm_input), intern.hpp helpers
 * (focus, arrange, tree*, resize), and x11/wm_ops.hpp for XCB operations used by handlers.
 *
 * - Runtime entrypoints: resolve `wm::state::monitor_or_fallback(state)` at boundary; no `monitor_from_state` alias.
 * - Kept-with-rationale: `bsp_swap_clients_directional`, `bsp_split_client_out_of_group` — shared by `swapwindow`,
 *   `moveoutofgroup`, and `movewindoworgroup` tree branches (single implementation, MUST group tests).
 * - `focusurgent_scan_monitor_chain` takes `MonitorState&` for chain head + mutates `runtime_state.monitors.current`.
 * - `focusmonitor` / `movetomonitor` take `MonitorState&` (explicit head + current).
 */
#include "actions.hpp"
#include "actions/boundary_dispatch.hpp"
#include "monitor/monitor_model.hpp"
#include "actions/focus_cycle.hpp"
#include "actions/workspace.hpp"
#include "bsp/add_flow.hpp"
#include "bsp/workspace_store.hpp"
#include "client/client_focus.hpp"
#include "client/client_props.hpp"
#include "dispatch/xcb_handlers.hpp"
#include "intern.hpp"
#include "monitor/world_state.hpp"
#include "config.hpp"
#include "bsp/tree_ops.hpp"
#include "layoutmsg.hpp"
#include "workspace_ref.hpp"
#include "workspace_registry.hpp"
#include "util.hpp"
#include "wm_layout_limits.hpp"
#include "context/monitor_context.hpp"
#include "monitor_select.hpp"
#include "state/wm_state_root.hpp"
#include "wm_state.hpp"
#include "x11/connection.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_server.hpp"
#include "sys/spawn.hpp"

#include <algorithm>
#include <utility>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

#include <xcb/xcb.h>

using wm::actions::boundary::command_int;
using wm::actions::boundary::command_spawn_args;
using wm::config::parse::ActionCommand;

/* Normalize movement/focus direction tokens to lowercase ASCII command keys. */
[[nodiscard]] static int normalize_direction_key(int dir_char) noexcept {
    return static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(dir_char)));
}

/* Treat grouped leaf as tab group when it has multiple clients or explicit groupmode intent. */
[[nodiscard]] static bool leaf_has_group_semantics(const LayoutNode* leaf) {
    return leaf && leaf->type == NODE_GROUPED && (leaf->grouped.clients.size() > 1U || leaf->grouped.groupmode);
}

/* Return selected client for monitor-local action helpers. */
[[nodiscard]] static Client* selected_client_from_monitor(Monitor* current) noexcept {
    return current ? current->sel : nullptr;
}

/* Tree-only split ratio adjustment using an already-resolved monitor. */
static void splitratio_adjust(Monitor* current, float payload) {
    LayoutNode* split = nullptr;
    Client*     sel   = selected_client_from_monitor(current);
    float       ratio;

    if (!current || !monitor_arrange_fn(current))
        return;
    if (monitor_arrange_fn(current) != tree || !sel)
        return;
    split = bsp_active_split(sel);
    if (!split)
        return;
    ratio = payload < 1.0f ? static_cast<float>(payload + split->split.ratio) : static_cast<float>(payload - 1.0f);
    if (bsp_set_split_ratio(split, std::clamp(ratio, kSplitRatioMinF, kSplitRatioMaxF)))
        arrange(current);
}

/* Handle parsed layoutmsg payload using an already-resolved monitor. */
static void layoutmsg_apply(Monitor* current, const LayoutMsgPayload& payload) {
    switch (payload.kind) {
        case LayoutMsgKind::SplitRatioDelta: splitratio_adjust(current, payload.value); break;
        case LayoutMsgKind::SplitRatioExact: splitratio_adjust(current, 1.0f + payload.value); break;
        case LayoutMsgKind::SwapSplit: {
            Client* sel = current ? current->sel : nullptr;
            if (!current || monitor_arrange_fn(current) != tree || !sel)
                return;
            LayoutNode* split = bsp_active_split(sel);
            if (!split || split->type != NODE_SPLIT)
                return;
            std::swap(split->split.first, split->split.second);
            arrange(current);
            break;
        }
        case LayoutMsgKind::ToggleSplit: {
            if (!g_config.dwindle_preserve_split)
                return;
            Client* sel = current ? current->sel : nullptr;
            if (!current || monitor_arrange_fn(current) != tree || !sel)
                return;
            LayoutNode* split = bsp_active_split(sel);
            if (!split || split->type != NODE_SPLIT)
                return;
            split->split.axis = (split->split.axis == SPLIT_VERTICAL) ? SPLIT_HORIZONTAL : SPLIT_VERTICAL;
            arrange(current);
            break;
        }
        case LayoutMsgKind::Preselect: {
            const int dir = payload.extra;
            if (dir == 'l' || dir == 'r' || dir == 'u' || dir == 'd' || dir == 't' || dir == 'b')
                bsp_set_preselect_dir(dir);
            break;
        }
        case LayoutMsgKind::MoveToRoot: {
            Client* sel = current ? current->sel : nullptr;
            if (!current || monitor_arrange_fn(current) != tree || !sel || sel->isfloating || !sel->leaf || sel->leaf->type != NODE_GROUPED)
                return;

            const int       unstable       = payload.extra ? 1 : 0;
            const int       selected_first = unstable ? 0 : 1;
            const float     root_ratio     = selected_first ? kSplitRatioMaxF : kSplitRatioMinF;
            const SplitAxis axis           = (sel->w >= sel->h) ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;

            bsp_remove_client(sel);
            const WorkspaceRef viewed_ws = WorkspaceRef::normal(current->active_workspace_id);
            auto               remaining = BspWorkspaceStore(*current).take(viewed_ws);
            auto               newleaf   = lt_new_grouped();
            if (!newleaf || !lt_grouped_add(newleaf.get(), sel)) {
                if (remaining)
                    BspWorkspaceStore(*current).set(viewed_ws, std::move(remaining));
                bsp_add_client(sel, current);
                return;
            }
            LayoutNode* const newleaf_obs = newleaf.get();
            sel->leaf                     = newleaf_obs;

            if (!remaining) {
                MonitorWorldState(*current).set_viewed_active(std::move(newleaf));
                arrange(current);
                return;
            }

            auto split = selected_first ? lt_new_split(axis, root_ratio, std::move(newleaf), std::move(remaining)) :
                                          lt_new_split(axis, root_ratio, std::move(remaining), std::move(newleaf));
            if (!split) {
                bsp_add_client(sel, current);
                return;
            }
            MonitorWorldState(*current).set_viewed_active(std::move(split));
            arrange(current);
            break;
        }
    }
}

/* Resolve directional target client for l/r/u/d navigation used by swap/move group-aware dispatchers. */
[[nodiscard]] static Client* directional_target_client(Monitor* m, Client* sel, int dir_char) {
    LayoutNode *cur, *split, *targetsub, *targetleaf;
    Client*     target = nullptr;

    if (!m || !sel)
        return nullptr;
    const int dir = normalize_direction_key(dir_char);
    switch (dir) {
        case 'l':
        case 'r':
            if (!sel->leaf || !sel->leaf->parent)
                break;
            split = sel->leaf->parent;
            if (split->type != NODE_SPLIT)
                break;
            cur = sel->leaf;
            while (cur->parent && cur->parent != split)
                cur = cur->parent;
            if (dir == 'r')
                targetsub = (split->split.second.get() == cur) ? split->split.first.get() : split->split.second.get();
            else
                targetsub = (split->split.first.get() == cur) ? split->split.second.get() : split->split.first.get();
            targetleaf = BspTreeOps::first_grouped(targetsub);
            if (targetleaf && targetleaf->type == NODE_GROUPED && targetleaf->grouped.clients.size() > 0U)
                target = lt_grouped_active(targetleaf);
            break;
        case 'd': target = cyclefocus_next_visible_after(m, sel); break;
        case 'u': target = cyclefocus_prev_visible_before(m, sel); break;
        default: break;
    }
    return target;
}

/* Tree/monitor helpers: WMState entrypoints call monitor_or_fallback(state) before these where needed. */
[[nodiscard]] static Monitor* dirtomon_from_chain(Monitor* head, Monitor* origin, int direction) noexcept;
static void                   apply_selected_layout(Monitor* current, const Layout* selected);
static void                   focusurgent_scan_monitor_chain(wm::state::MonitorState& monitors, wm::state::WMState& runtime_state);
static void                   bsp_swap_clients_directional(Monitor* current, int direction);
static void                   bsp_split_client_out_of_group(Monitor* current, int direction);
static void                   bsp_split_client_out_of_group_side(Monitor* current, int dir_char);
static Client*                resolve_movemouse_merge_target(Monitor* current, Client* dragged, Client* droptarget, Window pointer_child, int ptr_ok);
static void                   apply_movemouse_merge_on_drop(Monitor* current, Client* dragged, Client* merge_target);

/* Granular monitor-substate overload for dispatcher-first paths. */
void focusmonitor(wm::state::MonitorState& monitors, const int direction) {
    Monitor* current = wm::state::monitor_or_fallback(monitors);
    if (!current || wm::state::monitor_count() < 2U)
        return;
    Monitor* target = dirtomon_from_chain(monitors.first(), current, direction);
    if (!target || target == current)
        return;
    if (!switch_selected_monitor(monitors.current, target, true))
        return;
    current                      = target;
    const WorkspaceId ws_default = workspace_registry_default_for_monitor(current->num, monitor_select_output_name_for_num(current->num));
    if (ws_default >= kWorkspaceIdMin) {
        monitor_set_active_workspace_id(current, ws_default);
        MonitorWorldState(*current).sync_viewed_from_active_workspace();
    }
    focus(nullptr);
    update_net_desktop_props();
}

/* Focus first urgent client in monitor-chain order (monitors.first()) and reveal workspace when needed.
 * Mutates runtime_state.monitors.current when focus lands on monitor m. */
static void focusurgent_scan_monitor_chain(wm::state::MonitorState& /*monitors*/, wm::state::WMState& runtime_state) {
    Client* c;

    for (Monitor* m : wm::state::all_monitors()) {
        c = nullptr;
        for (Client* scan : m->clients) {
            if (scan->isurgent) {
                c = scan;
                break;
            }
        }
        if (!c)
            continue;
        runtime_state.monitors.current = m;
        if (!client_is_visible(c) && c->workspace.is_normal())
            static_cast<void>(view_workspace_id(runtime_state, c->workspace.normal_id));
        focus(c);
        restack(m);
        return;
    }
}

/* Detach one client from the BSP tree and append it to a grouped leaf (movemouse merge path).
 * On grouped-add failure, re-tiles the client on its monitor instead of leaving it orphaned. */
static void drag_merge_one_client_into_leaf(Client* client, LayoutNode* dstleaf, const WorkspaceRef& ws, Monitor* mon) {
    if (!client || !dstleaf || !mon)
        return;
    bsp_remove_client(client);
    client->workspace = ws;
    setclientworkspaceprop(client);
    if (lt_grouped_add_insert(dstleaf, client, g_config.group_insert_after_current))
        client->leaf = dstleaf;
    else
        bsp_add_client(client, mon);
}

/* Resolve the client under the pointer for movemouse merge-on-drop.
 * Mode 2 prefers the active tab of a groupbar slot; mode 1 uses droptarget with a live pointer fallback. */
static Client* resolve_movemouse_merge_target(Monitor* current, Client* dragged, Client* droptarget, Window pointer_child, int ptr_ok) {
    if (!current || !dragged)
        return nullptr;
    if (g_config.group_drag_into_group == 2) {
        LayoutNode* anchor = nullptr;
        if (ptr_ok) {
            for (const GroupbarSlot& slot : current->groupbars) {
                if (slot.win == pointer_child) {
                    anchor = slot.anchor;
                    break;
                }
            }
        }
        if (anchor && anchor->type == NODE_GROUPED && anchor->grouped.clients.size() > 0U) {
            Client* anchor_sel = lt_grouped_active(anchor);
            if (anchor_sel)
                return anchor_sel;
        }
        return nullptr;
    }
    Client* merge_target = droptarget;
    if (ptr_ok && pointer_child != None) {
        Client* target = wintoclient(pointer_child);
        if (target && target != dragged && target->mon == dragged->mon && !target->isfloating && client_is_visible(target))
            merge_target = target;
    }
    return merge_target;
}

/* Apply merge-on-drop policy after a movemouse drag: swap solo tiles, merge a whole source group, or append one client. */
static void apply_movemouse_merge_on_drop(Monitor* current, Client* dragged, Client* merge_target) {
    if (!current || !dragged || !merge_target || merge_target == dragged)
        return;
    if (!merge_target->leaf || merge_target->leaf->type != NODE_GROUPED)
        return;

    LayoutNode* const srcleaf    = dragged->leaf;
    LayoutNode* const dstleaf    = merge_target->leaf;
    const bool        srcgrouped = leaf_has_group_semantics(srcleaf);
    const bool        dstgrouped = leaf_has_group_semantics(dstleaf);

    if (!srcgrouped && !dstgrouped) {
        if (bsp_swap_clients(dragged, merge_target))
            arrange(current);
        return;
    }

    if (g_config.group_merge_groups_on_drag && srcleaf && srcleaf->type == NODE_GROUPED && srcleaf != dstleaf && srcleaf->grouped.clients.size() > 1U) {
        std::vector<Client*> members;
        members.reserve(srcleaf->grouped.clients.size());
        for (Client* mc : srcleaf->grouped.clients)
            members.push_back(mc);
        for (Client* mc : members) {
            if (!mc || mc->leaf != srcleaf)
                continue;
            drag_merge_one_client_into_leaf(mc, dstleaf, merge_target->workspace, current);
        }
        arrange(current);
        return;
    }

    drag_merge_one_client_into_leaf(dragged, dstleaf, merge_target->workspace, current);
    arrange(current);
}

/* Map pointer drag delta to peel side (`l`/`r`/`u`/`d`); ties prefer horizontal. */
[[nodiscard]] static int drag_delta_to_dir_char(int dx, int dy) noexcept {
    if (std::abs(dx) >= std::abs(dy))
        return dx >= 0 ? 'r' : 'l';
    return dy >= 0 ? 'd' : 'u';
}

/* True when release is not over another tiled client or any groupbar (empty/gap drop). */
[[nodiscard]] static bool movemouse_drop_is_empty(Monitor* current, Client* dragged, Window pointer_child, int ptr_ok) noexcept {
    if (!ptr_ok || pointer_child == None)
        return true;
    if (current) {
        for (const GroupbarSlot& slot : current->groupbars) {
            if (slot.win == pointer_child)
                return false;
        }
    }
    Client* const target = wintoclient(pointer_child);
    if (target && target != dragged && !target->isfloating && client_is_visible(target))
        return false;
    return true;
}

/*
 * Interactive move of selected client: pointer grab, motion loop via X11Backend, g_config.snap, optional tree swap
 * under pointer, merge-on-drop when g_config.group_drag_into_group is set. Empty-drop peel when
 * g_config.group_drag_out_of_group is set. Uses g_config.group_merge_groups_on_drag and lt_grouped_*; may send client to
 * another monitor after release.
 * Boundary entrypoint from pointer bindings (stateful drag loop).
 */
void movemouse(wm::state::WMState& rt) {
    int                                                  x, y, ocx, ocy, nx, ny;
    Client *                                             c, *target, *lastswap = nullptr, *droptarget = nullptr;
    Monitor*                                             m;
    std::optional<std::chrono::steady_clock::time_point> last_motion_tick;
    uint8_t                                              typ = 0;
    xcb_motion_notify_event_t*                           mo;
    xcb_motion_notify_event_t                            latest_motion;
    X11Backend*                                          backend_ctx;
    Monitor*                                             current = wm::state::monitor_or_fallback(rt);

    if (!current)
        return;
    c = current->sel;
    if (!c)
        return;
    backend_ctx = x11_backend_peek_context();
    if (!backend_ctx)
        return;
    if (c->isfullscreen) /* no support moving fullscreen windows by mouse */
        return;
    restack(current, *backend_ctx);
    ocx = c->x;
    ocy = c->y;
    if (wm::x11::grab_pointer(wm::x11::root_window(), false, kMouseEventMask, GrabModeAsync, GrabModeAsync, None, cursor[static_cast<std::size_t>(CursorKind::Move)]->cursor,
                              CurrentTime) != GrabSuccess)
        return;
    if (!getrootptr(&x, &y))
        return;
    do {
        auto                 gev_holder = backend_ctx->wait_masked_event(kMouseEventMask | ExposureMask | SubstructureRedirectMask);
        xcb_generic_event_t* gev        = gev_holder.get();
        if (!gev) {
            typ = XCB_BUTTON_RELEASE;
            break;
        }
        typ = gev->response_type & 0x7f;
        switch (typ) {
            case XCB_CONFIGURE_REQUEST:
            case XCB_EXPOSE:
            case XCB_MAP_REQUEST: dispatch_event(gev, rt, *backend_ctx); break;
            case XCB_MOTION_NOTIFY:
                mo            = reinterpret_cast<xcb_motion_notify_event_t*>(gev);
                latest_motion = *mo;
                ignore_result(backend_ctx->drain_latest_motion(latest_motion));
                mo = &latest_motion;
                /* Keep floating drag fully responsive; retain throttle for tree/tiling-heavy paths. */
                if (!c->isfloating) {
                    int draghz = std::min(g_config.refreshrate, kWmMoveMotionMaxHz);
                    if (draghz < 1)
                        draghz = 1;
                    const auto interval = std::chrono::milliseconds(1000 / draghz);
                    const auto now      = std::chrono::steady_clock::now();
                    if (last_motion_tick && (now - *last_motion_tick) < interval)
                        break;
                    last_motion_tick = now;
                }

                /* Use root-space pointer deltas for smooth drag across window boundaries. */
                nx = ocx + (mo->root_x - x);
                ny = ocy + (mo->root_y - y);
                if (g_config.snap_enabled && abs(current->wx - nx) < static_cast<int>(g_config.snap))
                    nx = current->wx;
                else if (g_config.snap_enabled && abs((current->wx + current->ww) - (nx + client_outer_width(c))) < static_cast<int>(g_config.snap))
                    nx = current->wx + current->ww - client_outer_width(c);
                if (g_config.snap_enabled && abs(current->wy - ny) < static_cast<int>(g_config.snap))
                    ny = current->wy;
                else if (g_config.snap_enabled && abs((current->wy + current->wh) - (ny + client_outer_height(c))) < static_cast<int>(g_config.snap))
                    ny = current->wy + current->wh - client_outer_height(c);
                if (!c->isfloating && monitor_arrange_fn(current) == tree) {
                    target = nullptr;
                    if (const auto ptr = wm::x11::query_pointer(wm::x11::root_window()); ptr && ptr->same_screen && ptr->child != None)
                        target = wintoclient(ptr->child);
                    if (target && target != c && target->mon == c->mon && !target->isfloating && client_is_visible(target)) {
                        droptarget = target;
                        if (g_config.group_drag_into_group == 0) {
                            if (target != lastswap && bsp_swap_clients(c, target)) {
                                arrange(current);
                                lastswap = target;
                            }
                        }
                    } else {
                        droptarget = nullptr;
                        lastswap   = nullptr;
                    }
                }
                if (!monitor_arrange_fn(current) || c->isfloating) {
                    resize(c, nx, ny, c->w, c->h, 1);
                    if (c->isfloating)
                        zestwm_flush_connection(*backend_ctx);
                }
                break;
            default: break;
        }
    } while (typ != XCB_BUTTON_RELEASE);
    static_cast<void>(wm::x11::ungrab_pointer(CurrentTime));
    /* Merge-on-drop or empty-drop peel after release (single pointer query). */
    if (!c->isfloating && monitor_arrange_fn(current) == tree) {
        const auto   ptr    = wm::x11::query_pointer(wm::x11::root_window());
        const int    ptr_ok = (ptr && ptr->same_screen) ? 1 : 0;
        const Window child  = ptr_ok ? ptr->child : None;
        bool         merged = false;
        if (g_config.group_drag_into_group > 0) {
            Client* const merge_target = resolve_movemouse_merge_target(current, c, droptarget, child, ptr_ok);
            if (merge_target && merge_target->leaf && merge_target->leaf->type == NODE_GROUPED) {
                apply_movemouse_merge_on_drop(current, c, merge_target);
                merged = true;
            }
        }
        if (!merged && g_config.group_drag_out_of_group && movemouse_drop_is_empty(current, c, child, ptr_ok)) {
            LayoutNode* const leaf = c->leaf;
            if (leaf && leaf->type == NODE_GROUPED && leaf->grouped.clients.size() > 1U) {
                const int release_x = ptr_ok ? ptr->root_x : x;
                const int release_y = ptr_ok ? ptr->root_y : y;
                bsp_split_client_out_of_group_side(current, drag_delta_to_dir_char(release_x - x, release_y - y));
            }
        }
    }
    setclientworkspaceprop(c);
    m = recttomon_from_fallback(c->x, c->y, c->w, c->h, current);
    if (m != current) {
        sendmon(c, m);
        rt.monitors.current = m;
        focus(nullptr);
    }
}

/* Sets running=0; restart flag from int payload. */
void quit(const ActionCommand* arg) {
    restart = command_int(arg) != 0;
    running = 0;
}

void quit_wm_ipc_dispatch() noexcept {
    restart = 0;
    running = 0;
}

/* Interactive resize: floating size from pointer delta or tree split ratio from pointer delta.
 * Boundary entrypoint from pointer bindings (stateful drag loop). */
void resizemouse(wm::state::WMState& rt) {
    int                                                  nw, nh;
    float                                                basesplitratio = 0.0f, newsplitratio;
    Client*                                              c;
    Monitor*                                             m;
    LayoutNode*                                          split = nullptr;
    std::optional<std::chrono::steady_clock::time_point> last_motion_tick;
    int                                                  startx, starty;
    int                                                  ptr0x = 0, ptr0y = 0, ow = 0, oh = 0;
    uint8_t                                              typ = 0;
    xcb_motion_notify_event_t*                           mo;
    xcb_motion_notify_event_t                            latest_motion;
    X11Backend*                                          backend_ctx;
    Cur*                                                 resize_cursor = cursor[static_cast<std::size_t>(CursorKind::Resize)];
    Monitor*                                             current       = wm::state::monitor_or_fallback(rt);

    if (!current)
        return;
    c = current->sel;
    if (!c)
        return;
    backend_ctx = x11_backend_peek_context();
    if (!backend_ctx)
        return;
    if (c->isfullscreen) /* no support resizing fullscreen windows by mouse */
        return;
    restack(current, *backend_ctx);
    if (!c->isfloating && monitor_arrange_fn(current) == tree) {
        split = bsp_active_split(c);
        if (!split) {
            return;
        }
        if (split->split.axis == SPLIT_VERTICAL)
            resize_cursor = cursor[static_cast<std::size_t>(CursorKind::ResizeH)];
        else
            resize_cursor = cursor[static_cast<std::size_t>(CursorKind::ResizeV)];
    }
    if (wm::x11::grab_pointer(wm::x11::root_window(), false, kMouseEventMask, GrabModeAsync, GrabModeAsync, None, resize_cursor->cursor, CurrentTime) != GrabSuccess)
        return;
    if (!c->isfloating && monitor_arrange_fn(current) == tree) {
        if (!getrootptr(&startx, &starty)) {
            static_cast<void>(wm::x11::ungrab_pointer(CurrentTime));
            return;
        }
        basesplitratio = split->split.ratio;
    } else if (c->isfloating || monitor_arrange_fn(current) == nullptr) {
        if (!getrootptr(&ptr0x, &ptr0y)) {
            static_cast<void>(wm::x11::ungrab_pointer(CurrentTime));
            return;
        }
        ow = c->w;
        oh = c->h;
    }
    do {
        auto                 gev_holder = backend_ctx->wait_masked_event(kMouseEventMask | ExposureMask | SubstructureRedirectMask);
        xcb_generic_event_t* gev        = gev_holder.get();
        if (!gev) {
            typ = XCB_BUTTON_RELEASE;
            break;
        }
        typ = gev->response_type & 0x7f;
        switch (typ) {
            case XCB_CONFIGURE_REQUEST:
            case XCB_EXPOSE:
            case XCB_MAP_REQUEST: dispatch_event(gev, rt, *backend_ctx); break;
            case XCB_MOTION_NOTIFY:
                mo            = reinterpret_cast<xcb_motion_notify_event_t*>(gev);
                latest_motion = *mo;
                ignore_result(backend_ctx->drain_latest_motion(latest_motion));
                mo = &latest_motion;
                /* Same motion throttle policy as movemouse (see g_config.refreshrate cap). */
                {
                    int draghz = std::min(g_config.refreshrate, kWmDragMotionMaxHz);
                    if (draghz < 1)
                        draghz = 1;
                    const auto interval = std::chrono::milliseconds(1000 / draghz);
                    const auto now      = std::chrono::steady_clock::now();
                    if (last_motion_tick && (now - *last_motion_tick) < interval)
                        break;
                    last_motion_tick = now;
                }

                if (!monitor_arrange_fn(current) || c->isfloating) {
                    nw = std::max(ow + (mo->root_x - ptr0x), kWmMinWindowDim);
                    nh = std::max(oh + (mo->root_y - ptr0y), kWmMinWindowDim);
                    resize(c, c->x, c->y, nw, nh, 1);
                } else if (monitor_arrange_fn(current) == tree && split) {
                    if (split->split.axis == SPLIT_VERTICAL) {
                        if (current->ww > 0) {
                            newsplitratio = basesplitratio + static_cast<float>(mo->root_x - startx) / static_cast<float>(current->ww);
                            if (bsp_set_split_ratio(split, newsplitratio))
                                arrange(current);
                        }
                    } else if (current->wh > 0) {
                        newsplitratio = basesplitratio + static_cast<float>(mo->root_y - starty) / static_cast<float>(current->wh);
                        if (bsp_set_split_ratio(split, newsplitratio))
                            arrange(current);
                    }
                }
                break;
            default: break;
        }
    } while (typ != XCB_BUTTON_RELEASE);
    if (c->isfloating || monitor_arrange_fn(current) == nullptr) {
        /* Snap final geometry to client hints after interactive drag. */
        resize(c, c->x, c->y, c->w, c->h, 0);
    }
    if (c->isfloating || monitor_arrange_fn(current) == nullptr)
        setclientworkspaceprop(c);
    if (c->isfloating || !monitor_arrange_fn(current) || (monitor_arrange_fn(current) == tree && !c->isfloating)) {
        /* Keep pointer where user released (no warp). */
    }
    static_cast<void>(wm::x11::ungrab_pointer(CurrentTime));
    for (;;) {
        auto d_holder = backend_ctx->poll_masked_event(EnterWindowMask);
        if (!d_holder)
            break;
    }
    m = recttomon_from_fallback(c->x, c->y, c->w, c->h, current);
    if (m != current) {
        sendmon(c, m);
        rt.monitors.current = m;
        focus(nullptr);
    }
}

static void bsp_swap_clients_directional(Monitor* current, int direction) {
    Client *sel, *target;
    if (!current)
        return;
    sel = current->sel;
    if (!sel)
        return;
    if (sel->isfullscreen && g_config.lockfullscreen)
        return;
    if (sel->isfloating || monitor_arrange_fn(current) != tree)
        return;
    target = directional_target_client(current, sel, direction);
    if (!target || target == sel || target->isfloating || !client_is_visible(target))
        return;
    if (bsp_swap_clients(sel, target))
        arrange(current);
}

static void apply_selected_layout(Monitor* current, const Layout* selected) {
    if (!current)
        return;
    monitor_apply_layout(current, selected);
    arrange(current);
}

/* Fork child process and exec spawn argv payload. */
void spawn(const ActionCommand* arg) {
    /* Boundary entrypoint: spawn has no monitor-context semantics. */
    const auto* args = command_spawn_args(arg);
    if (!args || args->empty())
        return;
    wm::sys::spawn_detached_argv(wm::x11::connection(), *args);
}

/* Granular monitor-substate overload for dispatcher-first paths. */
void movetomonitor(wm::state::MonitorState& monitors, const int direction) {
    Monitor* current = wm::state::monitor_or_fallback(monitors);
    Client*  sel     = selected_client_from_monitor(current);
    if (!sel || wm::state::monitor_count() < 2U)
        return;
    Monitor* target = dirtomon_from_chain(monitors.first(), current, direction);
    if (!target)
        return;
    sendmon(sel, target);
}

/* Peel selected client out of a group into a split sibling on the given side (`l`/`r`/`u`/`d`). */
static void bsp_split_client_out_of_group_side(Monitor* current, int dir_char) {
    Client* sel           = selected_client_from_monitor(current);
    int     move_selected = 0;

    if (!sel)
        return;
    const int dir = normalize_direction_key(dir_char);
    if (dir != 'l' && dir != 'r' && dir != 'u' && dir != 'd')
        return;
    LayoutNode* leaf = sel->leaf;
    if (!leaf || leaf->type != NODE_GROUPED)
        return;
    auto newleaf = lt_new_grouped();
    if (!newleaf)
        return;
    LayoutNode* newleaf_obs = newleaf.get();
    if (leaf->grouped.clients.size() > 1) {
        if (!lt_grouped_remove(leaf, sel) || !lt_grouped_add(newleaf.get(), sel)) {
            return;
        }
        leaf->grouped.groupmode    = 1;
        newleaf->grouped.groupmode = 0;
        move_selected              = 1;
    }
    LayoutNode* const           parent         = leaf->parent;
    const bool                  vertical       = (dir == 'l' || dir == 'r');
    const bool                  newleaf_first  = (dir == 'l' || dir == 'u');
    const SplitAxis             axis           = vertical ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
    const float                 ratio          = bsp_default_split_ratio();
    const bool                  leaf_was_first = parent && parent->split.first.get() == leaf;
    std::unique_ptr<LayoutNode> leaf_owned;
    if (parent)
        leaf_owned = lt_steal_child(parent, leaf);
    else
        leaf_owned = BspWorkspaceStore(*current).take(WorkspaceRef::normal(current->active_workspace_id));
    if (!leaf_owned)
        return;
    auto split = newleaf_first ? lt_new_split(axis, ratio, std::move(newleaf), std::move(leaf_owned)) : lt_new_split(axis, ratio, std::move(leaf_owned), std::move(newleaf));
    if (!split)
        return;
    if (!parent)
        MonitorWorldState(*current).set_viewed_active(std::move(split));
    else if (leaf_was_first)
        lt_attach_first(parent, std::move(split));
    else
        lt_attach_second(parent, std::move(split));
    if (move_selected)
        sel->leaf = newleaf_obs;
    arrange(current);
}

/* Legacy int direction: >0 vertical (new leaf right), <=0 horizontal (new leaf below). */
static void bsp_split_client_out_of_group(Monitor* current, int direction) {
    bsp_split_client_out_of_group_side(current, direction > 0 ? 'r' : 'd');
}

/* Resolve directional monitor hop using authority-owned monitor order. */
[[nodiscard]] static Monitor* dirtomon_from_chain(Monitor* /*head*/, Monitor* origin, const int direction) noexcept {
    const std::vector<Monitor*> mons = wm::state::all_monitors();
    if (mons.empty() || !origin)
        return nullptr;
    std::size_t idx = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < mons.size(); ++i) {
        if (mons[i] == origin) {
            idx = i;
            break;
        }
    }
    if (idx == static_cast<std::size_t>(-1))
        return nullptr;
    if (direction > 0)
        return mons[(idx + 1U) % mons.size()];
    return mons[(idx + mons.size() - 1U) % mons.size()];
}

/* Runtime WMState entrypoints (dispatcher + focus_cycle). */
void focussplit(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    Client*        sel     = selected_client_from_monitor(current);

    if (!sel || direction == 0)
        return;
    /* Left/right split navigation reuses directional_target_client's l/r sibling-subtree
     * computation (direction > 0 == 'r', < 0 == 'l'); it returns null on any guard miss. */
    Client* target = directional_target_client(current, sel, direction > 0 ? 'r' : 'l');
    if (target) {
        focus(target);
        restack(current);
    }
}

void focusgroup(wm::state::WMState& state, const int slot) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    LayoutNode*    leaf;
    Client*        c;
    int            idx, remaining;

    if (!current)
        return;
    leaf = current->group_anchor;
    if (!leaf || leaf->type != NODE_GROUPED)
        return;
    if (slot < 0)
        return;
    remaining = slot;
    for (idx = 0; idx < static_cast<int>(leaf->grouped.clients.size()); idx++) {
        c = leaf->grouped.clients[idx];
        if (!c || !client_is_visible(c))
            continue;
        if (remaining-- == 0) {
            focus(c);
            restack(current);
            return;
        }
    }
}

void cyclegroup(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    LayoutNode*    leaf;
    Client*        sel  = selected_client_from_monitor(current);
    Client*        next = nullptr;
    int            dir, i, idx, cur, len;

    if (direction == 0)
        return;
    if (!sel)
        return;
    leaf = sel->leaf;
    if (!leaf || leaf->type != NODE_GROUPED)
        return;
    if (leaf->grouped.clients.size() < 2)
        return;
    dir = direction > 0 ? 1 : -1;
    len = static_cast<int>(leaf->grouped.clients.size());
    cur = -1;
    for (i = 0; i < len; i++) {
        if (leaf->grouped.clients[i] == sel) {
            cur = i;
            break;
        }
    }
    if (cur < 0 || cur >= len)
        cur = static_cast<int>(leaf->grouped.active);
    idx = cur;
    for (i = 0; i < len - 1; i++) {
        idx  = (idx + dir + len) % len;
        next = leaf->grouped.clients[idx];
        if (!next || !client_is_visible(next))
            continue;
        focus(next);
        arrange(current);
        return;
    }
}

void focusurgent(wm::state::WMState& state) {
    focusurgent_scan_monitor_chain(state.monitors, state);
}

void killclient(wm::state::WMState& state) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    Client*        sel     = selected_client_from_monitor(current);
    if (!sel)
        return;
    if (!sendevent(sel, wm::x11::wm_atom(WMDelete))) {
        wm::x11::grab_server();
        if (xcb_connection_t* const conn = wm::x11::connection()) {
            xcb_set_close_down_mode(conn, static_cast<uint8_t>(DestroyAll));
            xcb_kill_client(conn, static_cast<uint32_t>(sel->win));
        }
        wm::x11::sync(false);
        wm::x11::ungrab_server();
        return;
    }
    zestwm_flush_connection();
}

void swapwindow(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    bsp_swap_clients_directional(current, direction);
}

void bringactivetotop(wm::state::WMState& state) {
    if (Monitor* const m = wm::state::monitor_or_fallback(state))
        restack(m);
}

void cyclelayout(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    size_t         i;

    if (!current || g_config.layouts.size() == 0)
        return;
    for (i = 0; i < g_config.layouts.size(); i++)
        if (&g_config.layouts[i] == monitor_active_layout(current))
            break;
    if (i == g_config.layouts.size())
        i = 0;
    i = (i + static_cast<size_t>(direction + static_cast<int>(g_config.layouts.size()))) % g_config.layouts.size();
    apply_selected_layout(current, &g_config.layouts[i]);
}

void setlayout(wm::state::WMState& state, const Layout* selected) {
    apply_selected_layout(wm::state::monitor_or_fallback(state), selected);
}

void splitratio(wm::state::WMState& state, const float payload) {
    splitratio_adjust(wm::state::monitor_or_fallback(state), payload);
}

void layoutmsg(wm::state::WMState& state, const LayoutMsgPayload& payload) {
    layoutmsg_apply(wm::state::monitor_or_fallback(state), payload);
}

void togglefloating(wm::state::WMState& state) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    Client*        c       = selected_client_from_monitor(current);
    if (!c)
        return;
    if (c->isfullscreen)
        return;
    c->isfloating = !c->isfloating || c->isfixed;
    setclientworkspaceprop(c);
    if (c->isfloating) {
        bsp_remove_client(c);
        resize(c, c->x, c->y, c->w, c->h, 0);
    } else {
        bsp_add_client(c, c->mon);
    }
    arrange(current);
    updatefloatingclientlist();
}

void togglefullscreen(wm::state::WMState& state) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    Client*        sel     = selected_client_from_monitor(current);
    if (!sel)
        return;
    setfullscreen(sel, !sel->isfullscreen);
}

void groupmode(wm::state::WMState& state) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    LayoutNode*    leaf    = nullptr;
    Client*        sel     = selected_client_from_monitor(current);

    if (!sel || monitor_arrange_fn(current) != tree)
        return;
    leaf = sel->leaf;
    if (!leaf || leaf->type != NODE_GROUPED)
        return;
    leaf->grouped.groupmode = !leaf->grouped.groupmode;
    if (!leaf->grouped.groupmode && leaf->grouped.clients.size() > 1U)
        bsp_untab_leaf(leaf);
    arrange(current);
}

void movegroup(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    Client*        sel     = selected_client_from_monitor(current);
    LayoutNode*    leaf;

    if (!sel || direction == 0)
        return;
    leaf = sel->leaf;
    if (leaf && leaf->type == NODE_GROUPED && lt_grouped_move_active(leaf, direction))
        arrange(current);
}

void sendtogroup(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    if (!current)
        return;
    Client*     target;
    Client*     sel = selected_client_from_monitor(current);
    LayoutNode *leaf, *srcleaf;

    if (!sel || direction == 0)
        return;
    target = nullptr;
    for (Client* scan_c : current->clients) {
        if (!client_is_visible(scan_c) || scan_c == sel)
            continue;
        target = scan_c;
        if (direction < 0)
            break;
    }
    if (!target || !target->leaf || target->leaf->type != NODE_GROUPED)
        return;
    srcleaf = sel->leaf;
    if (!srcleaf || srcleaf->type != NODE_GROUPED)
        return;
    if (!lt_grouped_remove(srcleaf, sel))
        return;
    sel->leaf = nullptr;
    leaf      = target->leaf;
    if (!lt_grouped_add(leaf, sel))
        return;
    leaf->grouped.groupmode = 1;
    sel->leaf               = leaf;
    bsp_focus_client(sel);
    arrange(current);
}

void moveoutofgroup(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    bsp_split_client_out_of_group(current, direction);
}

void movewindoworgroup(wm::state::WMState& state, const int direction) {
    Monitor* const current = wm::state::monitor_or_fallback(state);
    const int      dir     = normalize_direction_key(direction);
    Client*        target;
    Client*        sel = selected_client_from_monitor(current);
    LayoutNode *   srcleaf, *dstleaf;

    if (!sel)
        return;
    if (sel->isfullscreen && g_config.lockfullscreen)
        return;
    if (sel->isfloating || monitor_arrange_fn(current) != tree)
        return;
    if (dir != 'l' && dir != 'r' && dir != 'u' && dir != 'd')
        return;

    target = directional_target_client(current, sel, dir);
    if (target && target != sel && !target->isfloating && client_is_visible(target)) {
        dstleaf = target->leaf;
        srcleaf = sel->leaf;
        if (leaf_has_group_semantics(dstleaf) && srcleaf && srcleaf->type == NODE_GROUPED && srcleaf != dstleaf) {
            if (!lt_grouped_remove(srcleaf, sel))
                return;
            sel->leaf = nullptr;
            if (!lt_grouped_add(dstleaf, sel))
                return;
            dstleaf->grouped.groupmode = 1;
            sel->leaf                  = dstleaf;
            bsp_focus_client(sel);
            arrange(current);
            return;
        }
    }

    if (sel->leaf && leaf_has_group_semantics(sel->leaf)) {
        bsp_split_client_out_of_group_side(current, dir);
        return;
    }

    bsp_swap_clients_directional(current, dir);
}