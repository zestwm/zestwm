/*
 * Implementation of XCB generic event copy helpers used by runtime dispatch.
 */
#include "dispatch/runtime_dispatch_xcb_event.hpp"

#include <cstring>

namespace wm::dispatch::runtime {

    std::size_t xcb_generic_event_payload_bytes(const xcb_generic_event_t* ev) noexcept {
        if (!ev)
            return 0U;
        const uint8_t type = ev->response_type & 0x7FU;
        if (type == XCB_GE_GENERIC) {
            const auto* ge = reinterpret_cast<const xcb_ge_generic_event_t*>(ev);
            return sizeof(xcb_ge_generic_event_t) + static_cast<std::size_t>(ge->length) * sizeof(uint32_t);
        }
        return sizeof(xcb_generic_event_t);
    }

    XcbReplyPtr<xcb_generic_event_t> xcb_clone_generic_event(const xcb_generic_event_t* ev) {
        const std::size_t bytes = xcb_generic_event_payload_bytes(ev);
        if (!ev || bytes == 0U)
            return nullptr;
        auto copy = make_xcb_reply_ptr(static_cast<xcb_generic_event_t*>(std::malloc(bytes)));
        if (!copy)
            return nullptr;
        std::memcpy(copy.get(), ev, bytes);
        return copy;
    }

} // namespace wm::dispatch::runtime
