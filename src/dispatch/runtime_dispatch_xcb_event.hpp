/*
 * XCB generic event sizing/cloning helpers for the runtime dispatch queue.
 * Shared with unit tests to keep re-entrant event buffering logic covered.
 */
#pragma once

#include "x11/reply_ptr.hpp"

#include <cstddef>

#include <xcb/xcb.h>

namespace wm::dispatch::runtime {

    /* Byte length of the XCB event record starting at `ev` (0 when `ev` is null). */
    [[nodiscard]] std::size_t xcb_generic_event_payload_bytes(const xcb_generic_event_t* ev) noexcept;

    /* Heap-allocate a byte-identical copy of `ev`, or empty unique_ptr on OOM / invalid input. */
    [[nodiscard]] XcbReplyPtr<xcb_generic_event_t> xcb_clone_generic_event(const xcb_generic_event_t* ev);

} // namespace wm::dispatch::runtime
