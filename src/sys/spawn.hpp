/*
 * Detached process spawn helpers for config hooks, binds, and autostart.
 *
 * Role:
 * - Centralize fork/setsid/SIGCHLD reset and optional XCB fd close before exec.
 * - Keep parent non-blocking; children `_exit(127)` on exec failure.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <xcb/xcb.h>

namespace wm::sys {

    /* Child-only: close WM XCB fd (if any), setsid, restore SIGCHLD to default. */
    void prepare_detached_child(xcb_connection_t* xc) noexcept;

    /* Fork + `/bin/sh -c <cmd>` in a detached child. No-op on empty cmd or fork failure. */
    void spawn_detached_shell(xcb_connection_t* xc, std::string_view cmd) noexcept;

    /* Fork + execvp of `args` in a detached child. No-op on empty args or fork failure. */
    void spawn_detached_argv(xcb_connection_t* xc, const std::vector<std::string>& args) noexcept;

} // namespace wm::sys
