/*
 * Group focus policy when a tab leaves a grouped leaf (close / remove).
 *
 * Values:
 * - history: prev_active, then previous tab, then next (default; matches prior hardcoded path)
 * - previous / next: adjacent visible tab preference
 * - first / last: first or last remaining visible tab
 * - leave: no in-group fallback (global focus path)
 */
#pragma once

#include <cctype>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace wm::config {

    enum class GroupFocusRemovedPolicy : std::uint8_t {
        History = 0,
        Previous,
        Next,
        First,
        Last,
        Leave,
    };

    /* Parse `group.focus_removed_window` token; error string on unknown value. */
    [[nodiscard]] inline std::expected<GroupFocusRemovedPolicy, std::string> parse_group_focus_removed_policy(std::string_view raw) {
        std::string value;
        value.reserve(raw.size());
        for (const unsigned char ch : raw) {
            if (std::isspace(ch))
                continue;
            value.push_back(static_cast<char>(std::tolower(ch)));
        }
        if (value.size() >= 2U && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2U);
        if (value == "history")
            return GroupFocusRemovedPolicy::History;
        if (value == "previous" || value == "prev")
            return GroupFocusRemovedPolicy::Previous;
        if (value == "next")
            return GroupFocusRemovedPolicy::Next;
        if (value == "first")
            return GroupFocusRemovedPolicy::First;
        if (value == "last")
            return GroupFocusRemovedPolicy::Last;
        if (value == "leave" || value == "none")
            return GroupFocusRemovedPolicy::Leave;
        return std::unexpected(std::string("group.focus_removed_window must be history|previous|next|first|last|leave"));
    }

} // namespace wm::config
