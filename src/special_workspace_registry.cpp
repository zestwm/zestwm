#include "special_workspace_registry.hpp"

#include "config/parse/values.hpp"
#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

    /* Product/runtime cap for special tags.
     * This bounds slot cardinality and keeps hidden-id bridge space constrained and predictable. */
    inline constexpr std::size_t kMaxSpecialWorkspaces = 97U;
    /* Canonical hidden-id base for special workspace bridge ids.
     * Slot N maps to hidden id (base + N), preserving a dense reversible mapping. */
    inline constexpr WorkspaceId kSpecialHiddenIdBase = 32U;

    /* Ordered slot storage (index == slot id) for metadata and deterministic iteration/export. */
    std::vector<SpecialWorkspaceMeta> g_specials;
    /* Fast reverse lookup from special tag string to stable slot id. */
    std::unordered_map<std::string, std::uint8_t> g_tag_to_slot;

    /* Local ASCII trim helper for key/value token parsing. */
    [[nodiscard]] std::string_view trim_sv(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
            s.remove_suffix(1);
        return s;
    }

    /* Emit source-qualified warning for special-workspace policy parsing. */
    void warn_special_rule(const std::string& source_name, unsigned lineno, std::string_view msg) {
        std::string full = "zestwm: ";
        full.append(source_name);
        full += ':';
        full += std::to_string(lineno);
        full += ": special workspace rule: ";
        full.append(msg);
        wm::log::warn_and_log(full);
    }

    /* Locale-independent ASCII case-insensitive equality for short config keys. */
    [[nodiscard]] bool ascii_ieq(std::string_view a, std::string_view b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const unsigned char ca = static_cast<unsigned char>(a[i]);
            const unsigned char cb = static_cast<unsigned char>(b[i]);
            if (std::tolower(ca) != std::tolower(cb))
                return false;
        }
        return true;
    }

} // namespace

/* Clear all special workspace registry state (slots + reverse lookup). */
void special_workspace_registry_clear() {
    g_specials.clear();
    g_tag_to_slot.clear();
}

/* Ensure tag is present in registry and return its stable slot index. */
std::optional<std::uint8_t> special_workspace_registry_ensure_tag(std::string tag) {
    const auto it = g_tag_to_slot.find(tag);
    if (it != g_tag_to_slot.end())
        return it->second;
    if (g_specials.size() >= kMaxSpecialWorkspaces) {
        std::string msg = "zestwm: special workspace registry is full (97 tags); cannot register tag ";
        if (tag.empty())
            msg += "(empty default special)";
        else {
            msg.push_back('"');
            constexpr std::size_t kMax = 96U;
            const std::size_t     n    = std::min(tag.size(), kMax);
            msg.append(tag.data(), n);
            if (tag.size() > kMax)
                msg.append("…");
            msg.push_back('"');
        }
        wm::log::warn_and_log(msg);
        return std::nullopt;
    }
    const auto           slot = static_cast<std::uint8_t>(g_specials.size());
    SpecialWorkspaceMeta meta{};
    meta.tag = std::move(tag);
    g_specials.push_back(std::move(meta));
    g_tag_to_slot.emplace(g_specials.back().tag, slot);
    return slot;
}

/* Resolve slot for an already-registered tag (no insertion side effects). */
std::optional<std::uint8_t> special_workspace_registry_slot_by_tag(const std::string_view tag) {
    const auto it = g_tag_to_slot.find(std::string(tag));
    if (it == g_tag_to_slot.end())
        return std::nullopt;
    return it->second;
}

/* Convert special tag to canonical hidden workspace id bridge. */
std::optional<WorkspaceId> special_workspace_registry_hidden_id_by_tag(const std::string_view tag) {
    const std::optional<std::uint8_t> slot = special_workspace_registry_slot_by_tag(tag);
    if (!slot.has_value())
        return std::nullopt;
    return static_cast<WorkspaceId>(kSpecialHiddenIdBase + static_cast<WorkspaceId>(*slot));
}

/* Decode canonical hidden workspace id back to special slot (`32 + slot`). */
std::optional<std::uint8_t> special_workspace_registry_slot_by_hidden_id(const WorkspaceId hidden_id) {
    if (hidden_id < kSpecialHiddenIdBase)
        return std::nullopt;
    const WorkspaceId canonical_slot = hidden_id - kSpecialHiddenIdBase;
    if (canonical_slot > static_cast<WorkspaceId>(std::numeric_limits<std::uint8_t>::max()))
        return std::nullopt;
    const std::uint8_t slot = static_cast<std::uint8_t>(canonical_slot);
    if (static_cast<std::size_t>(slot) >= g_specials.size())
        return std::nullopt;
    return slot;
}

/* Lookup special metadata by tag through slot indirection. */
const SpecialWorkspaceMeta* special_workspace_registry_find_by_tag(const std::string_view tag) {
    const auto it = g_tag_to_slot.find(std::string(tag));
    if (it == g_tag_to_slot.end())
        return nullptr;
    const std::uint8_t slot = it->second;
    if (static_cast<std::size_t>(slot) >= g_specials.size())
        return nullptr;
    return &g_specials[static_cast<std::size_t>(slot)];
}

/* Number of currently registered special tags. */
std::size_t special_workspace_registry_count() {
    return g_specials.size();
}

/* Metadata by slot index for deterministic export/debug iteration. */
const SpecialWorkspaceMeta* special_workspace_registry_at(const std::size_t index) {
    if (index >= g_specials.size())
        return nullptr;
    return &g_specials[index];
}

/* Merge one `key:value` token into existing special tag metadata.
 * Unknown/invalid keys are warning-only to keep config parsing resilient. */
void special_workspace_registry_merge_rule_token(const std::string_view tag, const std::string_view token, const std::string& source_name, const unsigned lineno) {
    const auto it = g_tag_to_slot.find(std::string(tag));
    if (it == g_tag_to_slot.end()) {
        warn_special_rule(source_name, lineno, "unknown special tag (declare with `workspace = special:<tag>` first)");
        return;
    }
    const size_t colon = token.find(':');
    if (colon == std::string_view::npos) {
        warn_special_rule(source_name, lineno, "token must be 'key:value'");
        return;
    }
    const std::string_view key = trim_sv(token.substr(0, colon));
    const std::string_view val = trim_sv(token.substr(colon + 1));
    if (key.empty()) {
        warn_special_rule(source_name, lineno, "empty rule key");
        return;
    }
    SpecialWorkspaceMeta& meta = g_specials[static_cast<std::size_t>(it->second)];
    if (ascii_ieq(key, "dim_special")) {
        const auto d = wm::config::values::parse_double_val(val);
        if (!d)
            warn_special_rule(source_name, lineno, d.error());
        else {
            double v = *d;
            if (v < 0.0)
                v = 0.0;
            if (v > 1.0)
                v = 1.0;
            meta.rule_dim_special = v;
        }
    } else {
        std::string msg = "unknown key '";
        msg.append(key);
        msg.push_back('\'');
        warn_special_rule(source_name, lineno, msg);
    }
}
