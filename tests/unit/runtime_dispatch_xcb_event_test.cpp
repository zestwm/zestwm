/*
 * Unit tests for XCB generic event clone/size helpers used by runtime dispatch.
 */
#include "dispatch/runtime_dispatch_xcb_event.hpp"

#include <cassert>
#include <cstring>

int main() {
    using wm::dispatch::runtime::xcb_clone_generic_event;
    using wm::dispatch::runtime::xcb_generic_event_payload_bytes;

    assert(xcb_generic_event_payload_bytes(nullptr) == 0U);
    assert(!xcb_clone_generic_event(nullptr));

    alignas(16) unsigned char buf[256];
    std::memset(buf, 0, sizeof(buf));
    auto* ev               = reinterpret_cast<xcb_generic_event_t*>(buf);
    ev->response_type      = 2;
    const std::size_t base = xcb_generic_event_payload_bytes(ev);
    assert(base == sizeof(xcb_generic_event_t));

    auto cpy = xcb_clone_generic_event(ev);
    assert(cpy != nullptr);
    assert(std::memcmp(cpy.get(), ev, base) == 0);

    std::memset(buf, 0, sizeof(buf));
    ev->response_type          = XCB_GE_GENERIC;
    auto* ge                   = reinterpret_cast<xcb_ge_generic_event_t*>(buf);
    ge->length                 = 3; /* extension length: uint32_t units after fixed header */
    const std::size_t ge_bytes = xcb_generic_event_payload_bytes(ev);
    assert(ge_bytes == sizeof(xcb_ge_generic_event_t) + 3U * sizeof(uint32_t));

    cpy = xcb_clone_generic_event(ev);
    assert(cpy != nullptr);
    assert(std::memcmp(cpy.get(), ev, ge_bytes) == 0);

    return 0;
}
