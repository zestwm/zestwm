/*
 * Shared RAII wrapper for XCB reply/event buffers allocated by libc `free`.
 *
 * Why:
 * - XCB reply APIs return raw pointers with `free` ownership contract.
 * - Wrapping them in unique_ptr keeps early-return paths leak-free.
 * - Deleter is a default-constructible type so empty unique_ptr / `nullptr`
 *   returns compile cleanly (unlike `unique_ptr<T, decltype(&free)>`).
 */
#pragma once

#include <cstdlib>
#include <memory>

struct XcbFreeDeleter {
    void operator()(void* ptr) const noexcept {
        std::free(ptr);
    }
};

template <typename T>
using XcbReplyPtr = std::unique_ptr<T, XcbFreeDeleter>;

template <typename T>
[[nodiscard]] inline XcbReplyPtr<T> make_xcb_reply_ptr(T* ptr) noexcept {
    return XcbReplyPtr<T>(ptr);
}
