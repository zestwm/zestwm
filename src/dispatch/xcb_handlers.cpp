/* XCB event handlers and dispatch_event implementation.
 *
 * Role:
 * - Core X11 event routing.
 * - RandR ScreenChangeNotify and monitor topology refresh helpers.
 */
#include "dispatch/xcb_handlers.hpp"

#include "actions.hpp"
#include "actions/workspace.hpp"
#include "bsp/add_flow.hpp"
#include "client/client_focus.hpp"
#include "client/client_lifecycle.hpp"
#include "client/client_props.hpp"
#include "config.hpp"
#include "config/parse/action.hpp"
#include "context/monitor_context.hpp"
#include "draw/bar.hpp"
#include "geometry.hpp"
#include "intern.hpp"
#include "monitor/monitor_lifecycle.hpp"
#include "monitor/monitor_model.hpp"
#include "monitor/world_state.hpp"
#include "state/wm_state_root.hpp"
#include "zest_dispatch_ids.hpp"
#include "util.hpp"
#include "wm_state.hpp"
#include "x11/backend.hpp"
#include "x11/connection.hpp"
#include "x11/reply_ptr.hpp"
#include "x11/wm_input.hpp"
#include "x11/wm_ops.hpp"
#include "x11/wm_pointer.hpp"
#include "x11/wm_window.hpp"

#include <chrono>
#include <cstdint>
#include <csignal>
#include <unordered_set>

#include <X11/Xatom.h>
#include <xcb/randr.h>
#include <xcb/xproto.h>

namespace {
    uint8_t                     randr_event_base = 0;
    std::unordered_set<uint8_t> pressed_keys;

    int                         follow_mouse_focus_on_enter(void) {
        return g_config.wm_input.follow_mouse == 1;
    }

    int follow_mouse_focus_on_click(void) {
        return g_config.wm_input.follow_mouse != 3;
    }

    /* Map groupbar visible-tab index to client (same visibility filter as drawtab). */
    [[nodiscard]] Client* groupbar_visible_client_at(LayoutNode* leaf, int visible_idx) noexcept {
        if (!leaf || leaf->type != NODE_GROUPED || visible_idx < 0)
            return nullptr;
        int vis = 0;
        for (Client* c : leaf->grouped.clients) {
            if (!c || !client_is_visible(c))
                continue;
            if (vis == visible_idx)
                return c;
            ++vis;
        }
        return nullptr;
    }

    /* Focus the group tab under a groupbar click (visible-index from hit-test). */
    void focus_groupbar_tab(Monitor* m, LayoutNode* leaf, int visible_idx) noexcept {
        if (!m || !leaf)
            return;
        Client* target = groupbar_visible_client_at(leaf, visible_idx);
        if (!target || target->neverfocus)
            return;
        focus(target);
        arrange(m);
    }

} // namespace

void xcb_handlers_set_randr_event_base(uint8_t base) {
    randr_event_base = base;
}

static void dispatchcmd(uint32_t cmd, uint32_t val, X11Backend& backend) {
    wm::state::WMState state = wm::state::build_runtime_state_root();
    switch (cmd) {
        case ZEST_DISPATCH_FOCUSMON: focusmonitor(state.monitors, static_cast<int32_t>(val)); break;
        case ZEST_DISPATCH_KILLCLIENT: killclient(state); break;
        case ZEST_DISPATCH_TOGGLEFLOATING: togglefloating(state); break;
        case ZEST_DISPATCH_TOGGLEFULLSCREEN: togglefullscreen(state); break;
        case ZEST_DISPATCH_FOCUSURGENT: focusurgent(state); break;
        case ZEST_DISPATCH_QUIT: quit_wm_ipc_dispatch(); break;
        case ZEST_DISPATCH_TOGGLE_SPECIAL: {
            if (const std::optional<WorkspaceRef> target = consume_special_dispatch_workspace_ref(); target)
                view_workspace_ref(state, *target);
            break;
        }
        case ZEST_DISPATCH_MOVETOSPECIAL: {
            if (const std::optional<WorkspaceRef> target = consume_special_dispatch_workspace_ref(); target)
                movetoworkspace_ref(state, *target, true);
            break;
        }
        case ZEST_DISPATCH_FOCUSWINDOW: {
            Client* target = wintoclient(static_cast<Window>(val));
            if (!target)
                break;
            state.monitors.current = target->mon;
            Monitor* current       = wm::state::monitor_or_fallback(state);
            if (!client_is_visible_on_monitor(target, current)) {
                if (target->workspace.is_normal())
                    monitor_set_active_workspace_id(current, target->workspace.normal_id);
                MonitorWorldState(*current).sync_viewed_from_active_workspace();
                arrange(current);
                update_net_desktop_props();
            }
            focus(target);
            restack(current, backend);
            break;
        }
        case ZEST_DISPATCH_RELOAD: raise(SIGHUP); break;
        case ZEST_DISPATCH_SETLAYOUT:
            if (val < g_config.layouts.size()) {
                setlayout(state, &g_config.layouts[static_cast<size_t>(val)]);
            }
            break;
        case ZEST_DISPATCH_VIEW_WORKSPACE_ID: {
            static_cast<void>(view_workspace_id(state, static_cast<WorkspaceId>(val)));
            break;
        }
        case ZEST_DISPATCH_MOVETOWORKSPACE_ID: {
            movetoworkspace_id(state, static_cast<WorkspaceId>(val), true);
            break;
        }
        case ZEST_DISPATCH_SPLITRATIO_DELTA: splitratio(state, static_cast<float>(static_cast<int32_t>(val)) / 10000.0f); break;
        case ZEST_DISPATCH_SPLITRATIO_EXACT:
            /* splitratio uses arg->f >= 1.0 for absolute mode (target = arg->f - 1.0). */
            splitratio(state, 1.0f + static_cast<float>(static_cast<int32_t>(val)) / 10000.0f);
            break;
        case ZEST_DISPATCH_SWAPSPLIT: {
            layoutmsg(state, LayoutMsgPayload{.kind = LayoutMsgKind::SwapSplit, .value = 0.0f, .extra = 0});
            break;
        }
        case ZEST_DISPATCH_TOGGLESPLIT: {
            layoutmsg(state, LayoutMsgPayload{.kind = LayoutMsgKind::ToggleSplit, .value = 0.0f, .extra = 0});
            break;
        }
        case ZEST_DISPATCH_PRESELECT: {
            const int dir = static_cast<int>(static_cast<unsigned char>(val & 0xffU));
            layoutmsg(state, LayoutMsgPayload{.kind = LayoutMsgKind::Preselect, .value = 0.0f, .extra = dir});
            break;
        }
        case ZEST_DISPATCH_MOVETOROOT: {
            const int unstable = (val & 1U) ? 1 : 0;
            layoutmsg(state, LayoutMsgPayload{.kind = LayoutMsgKind::MoveToRoot, .value = 0.0f, .extra = unstable});
            break;
        }
        default: break;
    }
}
/* RandR extension event base (0 when RandR is absent); set in setup(). */
void buttonpress_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    unsigned int i, click;
    int          dynamic_click_index = 0;
    Client*      c;
    Monitor*     m;
    auto*        be = reinterpret_cast<xcb_button_press_event_t*>(ev);

    click            = static_cast<unsigned>(ClickTarget::RootWindow);
    Monitor* current = wm::state::monitor_or_fallback(state);
    if (!current)
        return;
    /* focus monitor if necessary */
    m = wintomon_from_fallback(be->event, current);
    if (m && m != current) {
        Monitor* previous = current;
        if (previous->sel)
            unfocus(previous->sel, 1);
        if (!switch_selected_monitor(state.monitors.current, m, false))
            return;
        current = m;
        focus(nullptr);
        current = m;
    }
    if (GroupbarSlot* slot = monitor_groupbar_slot_for_window(current, be->event); slot && slot->ntabs > 0) {
        int idx;
        current->group_anchor = slot->anchor;
        if (g_config.groupbar_position == 1 || g_config.groupbar_position == 2)
            idx = static_cast<int>(static_cast<unsigned long>(be->event_y) * slot->ntabs / static_cast<unsigned long>(std::max(slot->h, 1)));
        else
            idx = static_cast<int>(static_cast<unsigned long>(be->event_x) * slot->ntabs / static_cast<unsigned long>(std::max(slot->w, 1)));
        if (idx < 0)
            idx = 0;
        if (idx >= slot->ntabs)
            idx = slot->ntabs - 1;
        click               = static_cast<unsigned>(ClickTarget::GroupBar);
        dynamic_click_index = idx;
        /* Built-in: left-click selects the tab under the pointer (no config bind required). */
        if (be->detail == XCB_BUTTON_INDEX_1)
            focus_groupbar_tab(current, slot->anchor, idx);
    } else {
        c = wintoclient(be->event);
        if (c) {
            if (!c->neverfocus && follow_mouse_focus_on_click()) {
                focus(c);
                restack(c->mon, backend);
            }
            wm::x11::allow_events(ReplayPointer, CurrentTime);
            click = static_cast<unsigned>(ClickTarget::ClientWindow);
        }
    }
    for (i = 0; i < g_config.buttons.size(); i++) {
        if (click == g_config.buttons[i].click && g_config.buttons[i].func && g_config.buttons[i].button == be->detail &&
            cleanmask(g_config.buttons[i].mask) == cleanmask(be->state)) {
            if (click == static_cast<unsigned>(ClickTarget::WorkspaceBar) && g_config.buttons[i].command &&
                g_config.buttons[i].command->kind == wm::config::parse::ActionPayloadKind::Int &&
                std::get<wm::config::parse::IntPayload>(g_config.buttons[i].command->payload).value == 0) {
                auto command    = *g_config.buttons[i].command;
                command.payload = wm::config::parse::IntPayload{.value = dynamic_click_index};
                wm::config::parse::execute_action_command(command);
            } else if (g_config.buttons[i].command) {
                wm::config::parse::execute_action_command(*g_config.buttons[i].command);
            } else {
                wm::config::parse::execute_action_command({.fn = g_config.buttons[i].func, .kind = wm::config::parse::ActionPayloadKind::NoArg});
            }
        }
    }
}

void clientmessage_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    auto*   cme = reinterpret_cast<xcb_client_message_event_t*>(ev);
    Client* c;

    if (cme->format == 32 && cme->window == wm::x11::root_window() && cme->type == wm::x11::net_atom(NetCurrentDesktop)) {
        const unsigned int di        = static_cast<unsigned int>(cme->data.data32[0]);
        const WorkspaceId  target_id = static_cast<WorkspaceId>(di + 1U);
        if (target_id >= kWorkspaceIdMin) {
            static_cast<void>(view_workspace_id(state, target_id));
        }
        return;
    }
    if (cme->format == 32 && cme->window == wm::x11::root_window() && cme->type == wm::x11::net_atom(NetZestDispatch)) {
        uint32_t cmd = cme->data.data32[0];
        uint32_t val = cme->data.data32[1];

        dispatchcmd(cmd, val, backend);
        return;
    }
    c = wintoclient(cme->window);
    if (!c)
        return;
    if (cme->type == wm::x11::net_atom(NetWMState)) {
        if (static_cast<Atom>(cme->data.data32[1]) == wm::x11::net_atom(NetWMFullscreen) || static_cast<Atom>(cme->data.data32[2]) == wm::x11::net_atom(NetWMFullscreen)) {
            const int want_fs  = (cme->data.data32[0] == 1 /* _NET_WM_STATE_ADD */
                                  || (cme->data.data32[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen));
            const int suppress = client_wm_should_suppress_application_fullscreen(c);
            if (want_fs && suppress)
                return;
            setfullscreen(c, want_fs);
        }
    } else if (cme->type == wm::x11::net_atom(NetActiveWindow)) {
        Monitor*      current     = wm::state::monitor_or_fallback(state);
        Client* const current_sel = current ? current->sel : nullptr;
        /* Activation request should reveal target workspace even when focus-on-activate is disabled. */
        if (!client_is_visible(c)) {
            if (!c->isdock && c->workspace.is_special()) {
                wm_ensure_special_overlay_visible(c->mon, c->workspace.special_tag, true);
            } else if (c->workspace.is_normal() && !c->workspace_rule_silent) {
                static_cast<void>(view_workspace_id(state, c->workspace.normal_id));
            }
        }
        if (g_config.wm_misc.focus_on_activate) {
            if (client_is_visible(c))
                focus(c);
            else if (c != current_sel && !c->isurgent)
                seturgent(c, 1);
        } else if (c != current_sel && !c->isurgent)
            seturgent(c, 1);
    }
}

/* Re-sync monitor topology and re-layout after a geometry/topology change.
 * Caller must set sw/sh before calling (root ConfigureNotify or RandR screen change). */
static void handle_monitor_topology_changed(wm::state::WMState& state) {
    const int geom_dirty = updategeom(state.monitors);
    if (!geom_dirty)
        return;
    if (canvas)
        ignore_result(canvas->resize(static_cast<unsigned>(sw), static_cast<unsigned>(bh)));
    draw_bar_resize_canvas(static_cast<unsigned>(sw), static_cast<unsigned>(th));
    updategroupbarwin();
    for (Monitor* m : wm::state::all_monitors()) {
        for (Client* c : m->clients)
            if (c->isfullscreen)
                resizeclient_fullscreen_target(c);
    }
    focus(nullptr);
    arrange(nullptr);
}

/* Store root logical screen size and re-sync monitor topology + re-layout. */
static void apply_root_screen_size(wm::state::WMState& state, int width, int height) {
    sw = width;
    sh = height;
    handle_monitor_topology_changed(state);
}

/* React to a RandR ScreenChangeNotify: refresh root screen size, then re-sync
 * monitor topology + re-layout. Fired on monitor connect/disconnect/reconfig. */
static void handle_randr_screen_change(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    auto* rce = reinterpret_cast<xcb_randr_screen_change_notify_event_t*>(ev);
    apply_root_screen_size(state, static_cast<int>(rce->width), static_cast<int>(rce->height));
}

static void configurenotify_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    auto* ce = reinterpret_cast<xcb_configure_notify_event_t*>(ev);
    if (ce->window != wm::x11::root_window())
        return;
    apply_root_screen_size(state, static_cast<int>(ce->width), static_cast<int>(ce->height));
}

static void configurerequest_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    Client*                  c;
    Monitor*                 m;
    Monitor*                 current = wm::state::monitor_or_fallback(state);
    auto*                    cr      = reinterpret_cast<xcb_configure_request_event_t*>(ev);
    wm::x11::WindowConfigure wc;

    c = wintoclient(cr->window);
    if (c) {
        if (cr->value_mask & CWBorderWidth)
            c->bw = cr->border_width;
        else if (c->isfloating || !current || !monitor_arrange_fn(current)) {
            m                       = c->mon;
            const bool use_workarea = (c->workspace.is_special() && !c->isdock);
            const int  bx           = use_workarea ? static_cast<int>(m->wx) : m->mx;
            const int  by           = use_workarea ? static_cast<int>(m->wy) : m->my;
            const int  bw           = use_workarea ? static_cast<int>(m->ww) : static_cast<int>(m->mw);
            const int  bh           = use_workarea ? static_cast<int>(m->wh) : static_cast<int>(m->mh);
            if (cr->value_mask & CWX) {
                c->oldx = c->x;
                c->x    = bx + cr->x;
            }
            if (cr->value_mask & CWY) {
                c->oldy = c->y;
                c->y    = by + cr->y;
            }
            if (cr->value_mask & CWWidth) {
                c->oldw = c->w;
                c->w    = cr->width;
            }
            if (cr->value_mask & CWHeight) {
                c->oldh = c->h;
                c->h    = cr->height;
            }
            if ((c->x + c->w) > bx + bw && c->isfloating)
                c->x = bx + (bw / 2 - client_outer_width(c) / 2); /* center in x direction */
            if ((c->y + c->h) > by + bh && c->isfloating)
                c->y = by + (bh / 2 - client_outer_height(c) / 2); /* center in y direction */
            if ((cr->value_mask & (CWX | CWY)) && !(cr->value_mask & (CWWidth | CWHeight)))
                configure(c);
            if (client_is_visible(c))
                wm::x11::move_resize_window(c->win, c->x, c->y, c->w, c->h);
            else
                c->needresize = 1;
            if (c->isdock && client_is_visible(c))
                arrange(c->mon);
        } else
            configure(c);
    } else {
        wc.x            = cr->x;
        wc.y            = cr->y;
        wc.width        = cr->width;
        wc.height       = cr->height;
        wc.border_width = cr->border_width;
        wc.sibling      = cr->sibling;
        wc.stack_mode   = cr->stack_mode;
        wm::x11::configure_window(cr->window, cr->value_mask, wc);
    }
    wm::x11::sync(false);
    zestwm_flush_connection(backend);
}

void destroynotify_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    Client* c;
    auto*   de = reinterpret_cast<xcb_destroy_notify_event_t*>(ev);

    c = wintoclient(de->window);
    if (c)
        release_client(state, c, 1);
}

void enternotify_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    Client*  c;
    Monitor* m;
    auto*    en = reinterpret_cast<xcb_enter_notify_event_t*>(ev);

    if ((en->mode != NotifyNormal || en->detail == NotifyInferior) && en->event != wm::x11::root_window())
        return;
    c                = wintoclient(en->event);
    Monitor* current = wm::state::monitor_or_fallback(state);
    if (!current)
        return;
    m = c ? c->mon : wintomon_from_fallback(en->event, current);
    if (m != current) {
        if (!g_config.wm_misc.mouse_move_focuses_monitor)
            return;
        if (current->sel)
            unfocus(current->sel, 1);
        state.monitors.current = m;
        current                = m;
    } else if (!c || c == current->sel)
        return;
    /* EnterNotify may target root/bar windows (no client) after monitor switch. */
    if (!c || c->neverfocus)
        return;
    if (!follow_mouse_focus_on_enter())
        return;
    focus(c);
}

void expose_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    Monitor* m;
    auto*    ex      = reinterpret_cast<xcb_expose_event_t*>(ev);
    Monitor* current = wm::state::monitor_or_fallback(state);

    if (ex->count != 0)
        return;
    m = wintomon(ex->window);
    if (!m)
        return;
    if (monitor_groupbar_slot_for_window(m, ex->window))
        drawtab(m);
    else if (ex->window == m->confwin)
        drawconfigbanner(m, current);
}

/* there are some broken focus acquiring clients needing extra handling */
void focusin_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    auto*    fe      = reinterpret_cast<xcb_focus_in_event_t*>(ev);
    Monitor* current = wm::state::monitor_or_fallback(state);
    if (!current)
        return;

    /* Drop stale held-key state when focus ownership changes to avoid "stuck key" repeat-gating. */
    pressed_keys.clear();
    if (current->sel && !current->sel->neverfocus && fe->event != current->sel->win)
        setfocus(current->sel);
}

void grabkeys(void) {
    updatenumlockmask();
    {
        unsigned int i, j, k;
        unsigned int modifiers[] = {0, LockMask, numlockmask, numlockmask | LockMask};
        int          start = 0, end = -1;
        int          skip     = 0;
        const Window root_win = wm::x11::root_window();

        static_cast<void>(wm::x11::ungrab_key(AnyKey, AnyModifier, root_win));
        wm::x11::display_keycode_range(&start, &end);
        if (end < start)
            return;
        const auto mapping = wm::x11::read_keyboard_mapping(static_cast<xcb_keycode_t>(start), end - start + 1);
        if (!mapping)
            return;
        skip = mapping->keysyms_per_keycode;
        for (i = 0; i < g_config.keys.size(); i++) {
            const bool ignore_mods = (g_config.keys[i].flags & BindFlagIgnoreMods) != 0U;
            if (g_config.keys[i].keycode != 0) {
                if (ignore_mods) {
                    static_cast<void>(wm::x11::grab_key(static_cast<xcb_keycode_t>(g_config.keys[i].keycode), AnyModifier, root_win, true, GrabModeAsync, GrabModeAsync));
                } else {
                    for (j = 0; j < std::size(modifiers); j++)
                        static_cast<void>(wm::x11::grab_key(static_cast<xcb_keycode_t>(g_config.keys[i].keycode), g_config.keys[i].mod | modifiers[j], root_win, true,
                                                            GrabModeAsync, GrabModeAsync));
                }
                continue;
            }
            for (k = static_cast<unsigned int>(start); k <= static_cast<unsigned int>(end); k++) {
                unsigned int col;

                for (col = 0; col < static_cast<unsigned int>(skip); col++)
                    if (g_config.keys[i].keysym == mapping->keysyms[(k - start) * skip + col])
                        break;
                if (col >= static_cast<unsigned int>(skip))
                    continue;
                if (ignore_mods) {
                    static_cast<void>(wm::x11::grab_key(static_cast<xcb_keycode_t>(k), AnyModifier, root_win, true, GrabModeAsync, GrabModeAsync));
                } else {
                    for (j = 0; j < std::size(modifiers); j++)
                        static_cast<void>(wm::x11::grab_key(static_cast<xcb_keycode_t>(k), g_config.keys[i].mod | modifiers[j], root_win, true, GrabModeAsync, GrabModeAsync));
                }
            }
        }
    }
}

/* Shared key-bind dispatch for press and release events (xcb_key_press/release_event_t are
 * layout-compatible: same detail/state fields). `is_release` selects which binds are eligible
 * (release-flagged vs not) and enables auto-repeat gating on press only. First matching bind
 * fires and stops the scan; mirrors the legacy keypress/keyrelease behavior. */
static void run_key_binds(xcb_generic_event_t* ev, bool is_release) {
    auto*               ke          = reinterpret_cast<xcb_key_press_event_t*>(ev);
    const uint8_t       detail      = static_cast<uint8_t>(ke->detail);
    const bool          was_pressed = pressed_keys.find(detail) != pressed_keys.end();
    const std::uint64_t now_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());

    /* Held-key set gates auto-repeat on press; release clears it. Updated before the scan
     * so `was_pressed` reflects the pre-event state (matches legacy ordering). */
    if (is_release)
        pressed_keys.erase(detail);
    else
        pressed_keys.insert(detail);

    /* Run a matched bind's command, then apply its cooldown and non-consuming replay. */
    auto fire = [&](Key& k) {
        if (k.command)
            wm::config::parse::execute_action_command(*k.command);
        else
            wm::config::parse::execute_action_command({.fn = k.func, .kind = wm::config::parse::ActionPayloadKind::NoArg});
        if (k.cooldown_ms > 0U)
            k.cooldown_until_ms = now_ms + static_cast<std::uint64_t>(k.cooldown_ms);
        if ((k.flags & BindFlagNonConsuming) != 0U)
            wm::x11::allow_events(ReplayKeyboard, CurrentTime);
    };

    for (unsigned int i = 0; i < g_config.keys.size(); i++) {
        Key&       k               = g_config.keys[i];
        const bool ignore_mods     = (k.flags & BindFlagIgnoreMods) != 0U;
        const bool trigger_release = (k.flags & BindFlagRelease) != 0U;
        const bool allow_repeat    = (k.flags & BindFlagRepeat) != 0U;
        const bool mod_match       = ignore_mods || (cleanmask(k.mod) == cleanmask(ke->state));

        if (trigger_release != is_release || !mod_match)
            continue;
        if (k.cooldown_ms > 0U && now_ms < k.cooldown_until_ms)
            continue;
        if (!k.func)
            continue;
        /* Auto-repeat gating applies only on press unless the bind opts into repeat. */
        const bool repeat_blocked = !is_release && !allow_repeat && was_pressed;
        if (k.keycode != 0) {
            if (detail != k.keycode)
                continue;
            if (repeat_blocked)
                continue;
            fire(k);
            return;
        }
        for (int col = 0;; col++) {
            const KeySym ks = wm::x11::keysym_for_keycode(static_cast<KeyCode>(ke->detail), col);
            if (ks == NoSymbol)
                break;
            if (ks != k.keysym)
                continue;
            if (repeat_blocked)
                break;
            fire(k);
            return;
        }
    }
}

void keypress(xcb_generic_event_t* ev) {
    run_key_binds(ev, /*is_release=*/false);
}

void keyrelease(xcb_generic_event_t* ev) {
    run_key_binds(ev, /*is_release=*/true);
}

void mappingnotify(xcb_generic_event_t* ev) {
    auto* me = reinterpret_cast<xcb_mapping_notify_event_t*>(ev);

    ignore_result(me);
    if (me->request == XCB_MAPPING_KEYBOARD)
        grabkeys();
}

void maprequest_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    auto*      mr = reinterpret_cast<xcb_map_request_event_t*>(ev);

    const auto info = wm::x11::read_window_info(mr->window);
    if (!info || info->override_redirect)
        return;
    if (!wintoclient(mr->window))
        adopt_client(state, mr->window, *info);
}

void motionnotify_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    static Monitor* mon = nullptr;
    Monitor*        m;
    auto*           mo       = reinterpret_cast<xcb_motion_notify_event_t*>(ev);
    Monitor*        fallback = wm::state::monitor_or_fallback(state);

    if (mo->event != wm::x11::root_window())
        return;
    m = recttomon_from_fallback(mo->root_x, mo->root_y, 1, 1, mon ? mon : fallback);
    if (m != mon && mon) {
        if (fallback && fallback->sel)
            unfocus(fallback->sel, 1);
        state.monitors.current = m;
        focus(nullptr);
    }
    mon = m;
}

void propertynotify_with_ctx(xcb_generic_event_t* ev, wm::state::WMState&, X11Backend& backend) {
    Client*      c;
    auto*        pe = reinterpret_cast<xcb_property_notify_event_t*>(ev);

    const Window root_win = backend.root != XCB_WINDOW_NONE ? static_cast<Window>(backend.root) : wm::x11::root_window();

    if ((pe->window == root_win) && (pe->atom == XA_WM_NAME))
        updatestatus();
    else if ((pe->window == root_win) && (pe->atom == wm::x11::net_atom(NetZestDispatch)) && pe->state != XCB_PROPERTY_DELETE) {
        uint32_t*               data;

        xcb_connection_t* const conn = backend.conn ? backend.conn : wm::x11::connection();
        if (!conn)
            return;
        xcb_get_property_cookie_t cookie =
            xcb_get_property(conn, 0, static_cast<xcb_window_t>(root_win), static_cast<xcb_atom_t>(wm::x11::net_atom(NetZestDispatch)), XCB_ATOM_CARDINAL, 0, 2);
        auto reply = make_xcb_reply_ptr(xcb_get_property_reply(conn, cookie, nullptr));
        if (!reply)
            return;
        if (reply->format != 32 || xcb_get_property_value_length(reply.get()) < static_cast<int>(2U * sizeof(uint32_t)))
            return;
        data = static_cast<uint32_t*>(xcb_get_property_value(reply.get()));
        dispatchcmd(data[0], data[1], backend);
    } else if (pe->state == XCB_PROPERTY_DELETE) {
        return; /* ignore */
    } else {
        c = wintoclient(pe->window);
        if (c) {
            switch (pe->atom) {
                default: break;
                case XA_WM_TRANSIENT_FOR:
                    /* Match adopt_client(): any non-null transient → float (parent need not be managed). */
                    if (!c->isfloating && wm::x11::read_transient_for(c->win)) {
                        c->isfloating = 1;
                        bsp_remove_client(c);
                        arrange(c->mon);
                        updatefloatingclientlist();
                    }
                    break;
                case XA_WM_NORMAL_HINTS: c->hintsvalid = 0; break;
                case XA_WM_HINTS:
                    updatewmhints(c);
                    drawtab(c->mon);
                    break;
            }
            if (pe->atom == XA_WM_NAME || pe->atom == wm::x11::net_atom(NetWMName)) {
                updatetitle(c);
                drawtab(c->mon);
            }
            if (pe->atom == wm::x11::net_atom(NetWMWindowType) || pe->atom == wm::x11::net_atom(NetWMState))
                updatewindowtype(c);
            if (c->isdock && pe->atom == wm::x11::net_atom(NetWMStrutPartial))
                arrange(c->mon);
        }
    }
}

void unmapnotify_with_ctx(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    static_cast<void>(backend);
    Client* c;
    auto*   ue = reinterpret_cast<xcb_unmap_notify_event_t*>(ev);

    c = wintoclient(ue->window);
    if (c) {
        if (ue->response_type & 0x80)
            setclientstate(c, WithdrawnState);
        else
            release_client(state, c, 0);
    }
}

void dispatch_event(xcb_generic_event_t* ev, wm::state::WMState& state, X11Backend& backend) {
    uint8_t t = ev->response_type & 0x7f;

    /* RandR events arrive offset by the extension's first_event base, so they
     * cannot be matched inside the core XCB_* switch below. Handle first. */
    if (randr_event_base && t == static_cast<uint8_t>(randr_event_base + XCB_RANDR_SCREEN_CHANGE_NOTIFY)) {
        handle_randr_screen_change(ev, state, backend);
        return;
    }

    switch (t) {
        case XCB_BUTTON_PRESS: buttonpress_with_ctx(ev, state, backend); break;
        case XCB_CLIENT_MESSAGE: clientmessage_with_ctx(ev, state, backend); break;
        case XCB_CONFIGURE_REQUEST: configurerequest_with_ctx(ev, state, backend); break;
        case XCB_CONFIGURE_NOTIFY: configurenotify_with_ctx(ev, state, backend); break;
        case XCB_DESTROY_NOTIFY: destroynotify_with_ctx(ev, state, backend); break;
        case XCB_ENTER_NOTIFY: enternotify_with_ctx(ev, state, backend); break;
        case XCB_EXPOSE: expose_with_ctx(ev, state, backend); break;
        case XCB_FOCUS_IN: focusin_with_ctx(ev, state, backend); break;
        case XCB_KEY_PRESS: keypress(ev); break;
        case XCB_KEY_RELEASE: keyrelease(ev); break;
        case XCB_MAPPING_NOTIFY: mappingnotify(ev); break;
        case XCB_MAP_REQUEST: maprequest_with_ctx(ev, state, backend); break;
        case XCB_MOTION_NOTIFY: motionnotify_with_ctx(ev, state, backend); break;
        case XCB_PROPERTY_NOTIFY: propertynotify_with_ctx(ev, state, backend); break;
        case XCB_UNMAP_NOTIFY: unmapnotify_with_ctx(ev, state, backend); break;
        default: break;
    }
}
