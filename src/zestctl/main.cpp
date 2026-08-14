/* zestctl CLI entry: argv parsing, X connection, dispatch/info command routing.
 * Query/info implementations live under `zestctl/query*`; X helpers in `zestctl/x11*`. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <string>
#include <vector>

#include <xcb/xcb.h>

#include "layoutmsg.hpp"
#include "zest_dispatch_ids.hpp"
#include "zestctl/helpers.hpp"
#include "zestctl/query.hpp"
#include "zestctl/x11.hpp"

/* Send a dispatch command and print `ok` in plain mode. */
static int run_dispatch_and_ack(xcb_connection_t* c, xcb_window_t root, uint32_t cmd, uint32_t val, int json) {
    if (send_dispatch(c, root, cmd, val) != 0)
        return 1;
    if (!json)
        printf("ok\n");
    return 0;
}

/* Parse `dispatch layoutmsg ...` and map it to dispatch id/value payload. */
static int parse_layoutmsg_dispatch(const std::vector<std::string>& t, uint32_t* out_cmd, uint32_t* out_val) {
    std::vector<std::string> layout_tokens(t.begin() + 2, t.end());
    const auto               parsed_layoutmsg = parse_layoutmsg_tokens(layout_tokens);
    if (!parsed_layoutmsg) {
        fprintf(stderr, "zestctl: %s\n", parsed_layoutmsg.error().c_str());
        return 1;
    }
    switch (parsed_layoutmsg->kind) {
        case LayoutMsgKind::SwapSplit:
            *out_cmd = ZEST_DISPATCH_SWAPSPLIT;
            *out_val = 0U;
            break;
        case LayoutMsgKind::ToggleSplit:
            *out_cmd = ZEST_DISPATCH_TOGGLESPLIT;
            *out_val = 0U;
            break;
        case LayoutMsgKind::Preselect:
            *out_cmd = ZEST_DISPATCH_PRESELECT;
            *out_val = static_cast<uint32_t>(static_cast<unsigned char>(parsed_layoutmsg->extra));
            break;
        case LayoutMsgKind::MoveToRoot:
            *out_cmd = ZEST_DISPATCH_MOVETOROOT;
            *out_val = parsed_layoutmsg->extra ? 1U : 0U;
            break;
        case LayoutMsgKind::SplitRatioExact:
            *out_cmd = ZEST_DISPATCH_SPLITRATIO_EXACT;
            *out_val = encode_fixed4(parsed_layoutmsg->value);
            break;
        case LayoutMsgKind::SplitRatioDelta:
            *out_cmd = ZEST_DISPATCH_SPLITRATIO_DELTA;
            *out_val = encode_fixed4(parsed_layoutmsg->value);
            break;
    }
    return 0;
}

/* Handle `dispatch ...` subcommands and translate them to WM payloads. */
static int run_dispatch_command(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json) {
    if (t.size() < 2)
        return 2;
    uint32_t cmd = 0, val = 0;

    if ((t[1] == "workspace" || t[1] == "view") && t.size() == 3) {
        if (t[2] == "next" || t[2] == "prev") {
            uint32_t cur = 0, total = 1, target;

            if (!get_workspace_meta(c, root, &cur, &total)) {
                fprintf(stderr, "zestctl: cannot read workspace state\n");
                return 1;
            }
            target = (t[2] == "next") ? ((cur + 1U) % total) : ((cur + total - 1U) % total);
            val    = target + 1U;
            cmd    = ZEST_DISPATCH_VIEW_WORKSPACE_ID;
        } else {
            std::string special_tag;
            if (parse_special_tag_from_workspace_token(t[2], &special_tag)) {
                if (set_special_dispatch_target(c, root, special_tag) != 0) {
                    fprintf(stderr, "zestctl: failed to set special dispatch target\n");
                    return 1;
                }
                cmd = ZEST_DISPATCH_TOGGLE_SPECIAL;
                val = 0U;
            } else {
                const auto parsed_ws = parse_workspace_id_or_name_token(c, root, t[2]);
                if (!parsed_ws)
                    return 2;
                cmd = ZEST_DISPATCH_VIEW_WORKSPACE_ID;
                val = *parsed_ws;
            }
        }
    } else if (t[1] == "movetoworkspace" && (t.size() == 3 || t.size() == 4)) {
        if (t[2] == "next" || t[2] == "prev") {
            uint32_t cur = 0, total = 1, target;

            if (!get_workspace_meta(c, root, &cur, &total)) {
                fprintf(stderr, "zestctl: cannot read workspace state\n");
                return 1;
            }
            target = (t[2] == "next") ? ((cur + 1U) % total) : ((cur + total - 1U) % total);
            val    = target + 1U;
            cmd    = ZEST_DISPATCH_MOVETOWORKSPACE_ID;
        } else {
            std::string special_tag;
            if (parse_special_tag_from_workspace_token(t[2], &special_tag)) {
                if (set_special_dispatch_target(c, root, special_tag) != 0) {
                    fprintf(stderr, "zestctl: failed to set special dispatch target\n");
                    return 1;
                }
                cmd = ZEST_DISPATCH_MOVETOSPECIAL;
                val = 0U;
            } else {
                const auto parsed_ws = parse_workspace_id_or_name_token(c, root, t[2]);
                if (!parsed_ws)
                    return 2;
                cmd = ZEST_DISPATCH_MOVETOWORKSPACE_ID;
                val = *parsed_ws;
            }
        }
        if (t.size() == 4) {
            const auto parsed_window = parse_window_id_token(t[3]);
            if (!parsed_window)
                return 2;
            if (send_dispatch(c, root, ZEST_DISPATCH_FOCUSWINDOW, *parsed_window) != 0)
                return 1;
            static_cast<void>(wait_for_active_window(c, root, *parsed_window));
        }
    } else if (t[1] == "focusmonitor" && t.size() == 3 && (t[2] == "+1" || t[2] == "-1")) {
        cmd = ZEST_DISPATCH_FOCUSMON;
        val = (t[2] == "+1") ? 1U : static_cast<uint32_t>(-1);
    } else if (t[1] == "killclient" && t.size() == 2) {
        cmd = ZEST_DISPATCH_KILLCLIENT;
    } else if (t[1] == "focusurgent" && t.size() == 2) {
        cmd = ZEST_DISPATCH_FOCUSURGENT;
    } else if (t[1] == "focuswindow" && t.size() == 3) {
        const auto parsed_window = parse_window_id_token(t[2]);
        if (!parsed_window)
            return 2;
        cmd = ZEST_DISPATCH_FOCUSWINDOW;
        val = *parsed_window;
    } else if (t[1] == "togglefloating" && t.size() == 2) {
        cmd = ZEST_DISPATCH_TOGGLEFLOATING;
    } else if (t[1] == "togglefullscreen" && t.size() == 2) {
        cmd = ZEST_DISPATCH_TOGGLEFULLSCREEN;
    } else if (t[1] == "reload" && t.size() == 2) {
        cmd = ZEST_DISPATCH_RELOAD;
    } else if (t[1] == "quit" && t.size() == 2) {
        cmd = ZEST_DISPATCH_QUIT;
    } else if (t[1] == "special" && t.size() == 3) {
        if (t[2].empty()) {
            fprintf(stderr, "zestctl: special tag must not be empty\n");
            return 1;
        }
        if (set_special_dispatch_target(c, root, t[2]) != 0) {
            fprintf(stderr, "zestctl: failed to set special dispatch target\n");
            return 1;
        }
        cmd = ZEST_DISPATCH_TOGGLE_SPECIAL;
        val = 0U;
    } else if ((t[1] == "layout" || t[1] == "setlayout") && t.size() == 3) {
        if (t[2] == "next" || t[2] == "prev") {
            std::vector<std::pair<uint32_t, std::string>> ll   = get_layout_list_for_dispatch(c, root);
            uint32_t                                      curi = 0;
            size_t                                        pos, i;

            if (ll.empty() || !get_current_layout_index(c, root, &curi)) {
                fprintf(stderr, "zestctl: cannot resolve current layout\n");
                return 1;
            }
            pos = 0;
            for (i = 0; i < ll.size(); i++) {
                if (ll[i].first == curi) {
                    pos = i;
                    break;
                }
            }
            if (t[2] == "next")
                pos = (pos + 1U) % ll.size();
            else
                pos = (pos + ll.size() - 1U) % ll.size();
            val = ll[pos].first;
        } else if (!resolve_layout_index(c, root, t[2], &val)) {
            fprintf(stderr, "zestctl: unknown layout '%s'\n", t[2].c_str());
            return 1;
        }
        cmd = ZEST_DISPATCH_SETLAYOUT;
    } else if (t[1] == "layoutmsg" && t.size() >= 3) {
        const int rc = parse_layoutmsg_dispatch(t, &cmd, &val);
        if (rc != 0)
            return rc;
    } else {
        return 2;
    }

    return run_dispatch_and_ack(c, root, cmd, val, json);
}

static int run_command(xcb_connection_t* c, xcb_window_t root, const std::vector<std::string>& t, int json) {
    uint32_t cmd = 0, val = 0;

    if (t.empty())
        return 2;
    if (t[0] == "dispatch") {
        return run_dispatch_command(c, root, t, json);
    }
    if (t[0] == "reload" && t.size() == 1) {
        cmd = ZEST_DISPATCH_RELOAD;
        val = 0U;
        return run_dispatch_and_ack(c, root, cmd, val, json);
    }
    if (t[0] == "quit" && t.size() == 1) {
        cmd = ZEST_DISPATCH_QUIT;
        val = 0U;
        return run_dispatch_and_ack(c, root, cmd, val, json);
    }
    return run_info(c, root, t, json);
}

int main(int argc, char** argv) {
    xcb_connection_t*        c;
    const xcb_setup_t*       setup;
    xcb_screen_iterator_t    it;
    xcb_screen_t*            screen;
    std::vector<std::string> args;
    int                      json = 0;
    int                      i, rc = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--json") == 0) {
            json = 1;
            continue;
        }
        args.push_back(argv[i]);
    }

    c = xcb_connect(nullptr, nullptr);
    if (!c || xcb_connection_has_error(c)) {
        fprintf(stderr, "zestctl: cannot connect to X server\n");
        if (c)
            xcb_disconnect(c);
        return 1;
    }
    setup = xcb_get_setup(c);
    it    = xcb_setup_roots_iterator(setup);
    if (it.rem == 0) {
        fprintf(stderr, "zestctl: no X screen found\n");
        xcb_disconnect(c);
        return 1;
    }
    screen = it.data;

    if (args.size() >= 2 && args[0] == "--batch") {
        std::vector<std::string> cmds = split_batch(args[1]);
        size_t                   bi;

        for (bi = 0; bi < cmds.size(); bi++) {
            rc = run_command(c, screen->root, split_ws(cmds[bi]), json);
            if (rc != 0)
                break;
        }
    } else {
        rc = run_command(c, screen->root, args, json);
    }

    if (rc == 2) {
        usage();
        rc = 1;
    }
    xcb_disconnect(c);
    return rc;
}
