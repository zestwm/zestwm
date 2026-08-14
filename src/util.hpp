/* Fatal error reporting for startup paths. */
#pragma once

#include <cstring>

#include <cstddef>
#include <string_view>
#include <type_traits>

void die(const char* fmt, ...);

/* Discard a return value on purpose (e.g. [[nodiscard]] API where the result is irrelevant). */
[[maybe_unused]] inline void ignore_result([[maybe_unused]] auto&&) noexcept {}

/* Iterate a NUL-separated buffer (root-property payload format) and invoke `fn`
 * for each non-empty entry. Handles `memchr` for the terminator, builds the entry
 * view, and advances `pos` past the NUL. Empty entries are skipped.
 * If `fn` returns `bool`, `false` stops iteration early (search-style callers).
 * Used by `_NET_ZEST_*` persistence parsers and `zestctl` query readers. */
template <typename Fn>
void for_each_nul_entry(const char* raw, std::size_t len, Fn&& fn) {
    std::size_t pos = 0U;
    while (pos < len) {
        const char*       nul = static_cast<const char*>(std::memchr(raw + pos, '\0', len - pos));
        const std::size_t n   = nul ? static_cast<std::size_t>(nul - (raw + pos)) : (len - pos);
        if (n != 0U) {
            using Result = std::invoke_result_t<Fn&, std::string_view>;
            if constexpr (std::is_same_v<Result, bool>) {
                if (!fn(std::string_view(raw + pos, n)))
                    return;
            } else {
                fn(std::string_view(raw + pos, n));
            }
        }
        pos += n + (nul ? 1U : 0U);
    }
}

/* Scan a `_NET_ZEST_TREE_STATE` payload for window-id tokens and invoke `fn` for each.
 * A token is a decimal run bounded by tree-state delimiters: `:` or `,` before, `,` or `)`
 * after. This excludes geometry values inside `|F(...)` suffixes and structural fields
 * inside `G(...)`/`S(...)` nodes that don't match the delimiter pair.
 * Callers must strip `|F(...)` beforehand if they only want tree-node window ids. */
template <typename Fn>
void for_each_window_id(std::string_view payload, Fn&& fn) {
    std::size_t i = 0U;
    while (i < payload.size()) {
        const unsigned char ch = static_cast<unsigned char>(payload[i]);
        if (ch < '0' || ch > '9') {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < payload.size()) {
            const unsigned char d = static_cast<unsigned char>(payload[i]);
            if (d < '0' || d > '9')
                break;
            ++i;
        }
        const std::size_t end      = i;
        const bool        has_prev = start > 0U;
        const bool        has_next = end < payload.size();
        const char        prev_ch  = has_prev ? payload[start - 1U] : '\0';
        const char        next_ch  = has_next ? payload[end] : '\0';
        if (!(prev_ch == ':' || prev_ch == ',') || !(next_ch == ',' || next_ch == ')'))
            continue;
        /* Parse the decimal run manually to avoid a std::string allocation per token. */
        unsigned long win = 0UL;
        for (std::size_t j = start; j < end; ++j)
            win = win * 10UL + static_cast<unsigned long>(payload[j] - '0');
        if (win != 0UL)
            fn(win);
    }
}
