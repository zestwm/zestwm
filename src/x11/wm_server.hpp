/* XCB server-wide grab helpers for race-sensitive client transitions. */
#pragma once

namespace wm::x11 {

    void grab_server();
    void ungrab_server();

} // namespace wm::x11
