#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

enum class LayoutMsgKind : std::uint8_t {
    SplitRatioDelta,
    SplitRatioExact,
    SwapSplit,
    ToggleSplit,
    Preselect,
    MoveToRoot
};

struct LayoutMsgPayload {
    LayoutMsgKind kind;
    float         value;
    int           extra;
};

/* Parse layoutmsg argv tail tokens ("splitratio ...", "preselect ...", etc.). */
[[nodiscard]] std::expected<LayoutMsgPayload, std::string> parse_layoutmsg_tokens(const std::vector<std::string>& tokens);
