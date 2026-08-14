#include "layoutmsg.hpp"

#include <cctype>
#include <cstdlib>

/* Parse strict floating-point token from user-supplied layoutmsg args. */
[[nodiscard]] static std::expected<float, std::string> parse_layoutmsg_float(const std::string& token) {
    char* end = nullptr;
    float v   = std::strtof(token.c_str(), &end);
    if (token.empty() || !end || *end != '\0')
        return std::unexpected("invalid splitratio value '" + token + "'");
    return v;
}

/* Splitratio value 1.0 means even split; normalize to 0..1 ratio. */
[[nodiscard]] static float normalize_layoutmsg_exact_ratio(float v) {
    if (v > 0.95f)
        return v / 2.0f;
    return v;
}

std::expected<LayoutMsgPayload, std::string> parse_layoutmsg_tokens(const std::vector<std::string>& tokens) {
    LayoutMsgPayload payload{.kind = LayoutMsgKind::SplitRatioDelta, .value = 0.0f, .extra = 0};

    if (tokens.empty())
        return std::unexpected("layoutmsg needs subcommand");

    if (tokens[0] == "swapsplit") {
        if (tokens.size() != 1)
            return std::unexpected("layoutmsg swapsplit takes no arguments");
        payload.kind = LayoutMsgKind::SwapSplit;
        return payload;
    }
    if (tokens[0] == "togglesplit") {
        if (tokens.size() != 1)
            return std::unexpected("layoutmsg togglesplit takes no arguments");
        payload.kind = LayoutMsgKind::ToggleSplit;
        return payload;
    }
    if (tokens[0] == "preselect") {
        if (tokens.size() != 2 || tokens[1].empty())
            return std::unexpected("layoutmsg preselect requires l|r|u|d|t|b");
        const int dir = static_cast<int>(static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(tokens[1][0]))));
        if (dir != 'l' && dir != 'r' && dir != 'u' && dir != 'd' && dir != 't' && dir != 'b')
            return std::unexpected("invalid preselect direction");
        payload.kind  = LayoutMsgKind::Preselect;
        payload.extra = dir;
        return payload;
    }
    if (tokens[0] == "movetoroot") {
        int unstable = 0;
        if (tokens.size() > 3)
            return std::unexpected("layoutmsg movetoroot accepts [active] [unstable]");
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i] == "active")
                continue;
            if (tokens[i] == "unstable") {
                unstable = 1;
                continue;
            }
            return std::unexpected("unsupported movetoroot argument '" + tokens[i] + "'");
        }
        payload.kind  = LayoutMsgKind::MoveToRoot;
        payload.extra = unstable;
        return payload;
    }
    if (tokens[0] == "splitratio") {
        if (tokens.size() != 2 && tokens.size() != 3)
            return std::unexpected("layoutmsg splitratio requires <value> [exact]");
        auto parsed = parse_layoutmsg_float(tokens[1]);
        if (!parsed)
            return std::unexpected(parsed.error());
        float v = *parsed;
        if (tokens.size() == 3) {
            if (tokens[2] != "exact")
                return std::unexpected("unsupported splitratio mode '" + tokens[2] + "'");
            payload.kind  = LayoutMsgKind::SplitRatioExact;
            payload.value = normalize_layoutmsg_exact_ratio(v);
            return payload;
        }
        payload.kind  = LayoutMsgKind::SplitRatioDelta;
        payload.value = v;
        return payload;
    }
    return std::unexpected("unsupported layoutmsg command '" + tokens[0] + "'");
}
