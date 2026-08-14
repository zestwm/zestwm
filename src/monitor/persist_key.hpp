/*
 * Monitor key helpers for tree-state persistence.
 *
 * Role:
 * - Format/parse the monitor field of `_NET_ZEST_TREE_STATE` entries.
 * - Support output-name keys and decimal Monitor.num keys for reload compat.
 */
#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace wm::monitor {

    /* Parsed monitor identity from a tree-state entry prefix. */
    struct PersistMonitorKey {
        bool          numeric = false;
        unsigned long num     = 0U;
        std::string   name;
    };

    /* True when `key` is a non-empty decimal digit string. */
    [[nodiscard]] inline bool persist_monitor_key_is_numeric(std::string_view key) noexcept {
        if (key.empty())
            return false;
        for (const unsigned char ch : key) {
            if (!std::isdigit(ch))
                return false;
        }
        return true;
    }

    /* Parse monitor key: all-digits → numeric id; otherwise output name. */
    [[nodiscard]] inline std::optional<PersistMonitorKey> parse_persist_monitor_key(std::string_view key) {
        if (key.empty())
            return std::nullopt;
        PersistMonitorKey out;
        if (persist_monitor_key_is_numeric(key)) {
            out.numeric = true;
            out.num     = 0U;
            for (const unsigned char ch : key) {
                out.num = out.num * 10U + static_cast<unsigned long>(ch - '0');
            }
            return out;
        }
        out.numeric = false;
        out.name    = std::string(key);
        return out;
    }

    /* Emit monitor key for save: output_name when set, else decimal num. */
    [[nodiscard]] inline std::string format_persist_monitor_key(std::string_view output_name, int num) {
        if (!output_name.empty())
            return std::string(output_name);
        return std::to_string(num);
    }

} // namespace wm::monitor
