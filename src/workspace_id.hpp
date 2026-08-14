#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

using WorkspaceId = std::uint32_t;

inline constexpr WorkspaceId kWorkspaceIdMin = 1U;

/* Parse decimal workspace id in runtime range 1..uint32_t max. */
[[nodiscard]] inline std::expected<WorkspaceId, std::string> parse_workspace_id(std::string_view token) {
    if (token.empty())
        return std::unexpected("workspace id is empty");
    WorkspaceId           value = 0;
    constexpr WorkspaceId kMax  = static_cast<WorkspaceId>(~WorkspaceId{0});
    for (const char ch : token) {
        if (ch < '0' || ch > '9')
            return std::unexpected("workspace id must be decimal");
        const WorkspaceId digit = static_cast<WorkspaceId>(ch - '0');
        if (value > (kMax - digit) / 10U)
            return std::unexpected("workspace id is too large");
        value = value * 10U + digit;
    }
    if (value < kWorkspaceIdMin)
        return std::unexpected("workspace id must be >= 1");
    return value;
}
