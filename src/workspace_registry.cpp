#include "workspace_registry.hpp"

#include "config/parse/values.hpp"
#include "log.hpp"
#include "special_workspace_registry.hpp"
#include "types.hpp"
#include "workspace_selector.hpp"

#include <cctype>
#include <utility>

namespace {
    /* Temporary in-memory workspace registry backing the WorkspaceId migration. */
    std::vector<WorkspaceMeta> g_workspace_registry;
    std::uint64_t              g_default_rank_counter = 0U;

    struct WorkspaceSelectorRuleEntry {
        std::string   selector;
        WorkspaceMeta policy{};
    };

    std::vector<WorkspaceSelectorRuleEntry> g_workspace_selector_rules;

    /* Generate fallback workspace names when config/legacy state does not provide one. */
    [[nodiscard]] std::string default_workspace_name(WorkspaceId id) {
        return std::to_string(id);
    }

    [[nodiscard]] WorkspaceMeta* meta_mut_by_id(WorkspaceId id) {
        if (id < kWorkspaceIdMin)
            return nullptr;
        if (g_workspace_registry.size() < static_cast<std::size_t>(id))
            return nullptr;
        return &g_workspace_registry[static_cast<std::size_t>(id - 1U)];
    }

    [[nodiscard]] std::string_view trim_sv(std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
            s.remove_suffix(1);
        return s;
    }

    void warn_rule(const std::string& source_name, unsigned lineno, std::string_view msg) {
        std::string full = "zestwm: ";
        full.append(source_name);
        full += ':';
        full += std::to_string(lineno);
        full += ": workspace rule: ";
        full.append(msg);
        wm::log::warn_and_log(full);
    }

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

    /* Overlay selector-rule policy fields onto a workspace meta copy (later rules win). */
    void merge_policy_fields(const WorkspaceMeta& src, WorkspaceMeta& dst) {
        if (src.rule_gaps_in)
            dst.rule_gaps_in = src.rule_gaps_in;
        if (src.rule_gaps_out)
            dst.rule_gaps_out = src.rule_gaps_out;
        if (src.rule_border_size)
            dst.rule_border_size = src.rule_border_size;
        if (src.rule_draw_border)
            dst.rule_draw_border = src.rule_draw_border;
        if (!src.rule_monitor_index.empty())
            dst.rule_monitor_index = src.rule_monitor_index;
        if (src.rule_default)
            dst.rule_default = src.rule_default;
        if (src.rule_persistent)
            dst.rule_persistent = src.rule_persistent;
        if (!src.rule_default_name.empty())
            dst.rule_default_name = src.rule_default_name;
        if (src.rule_default_rank != 0U)
            dst.rule_default_rank = src.rule_default_rank;
        if (!src.rule_layout_name.empty())
            dst.rule_layout_name = src.rule_layout_name;
        if (!src.on_created_empty_cmd.empty()) {
            dst.on_created_empty_cmd   = src.on_created_empty_cmd;
            dst.on_empty_spawn_pending = src.on_empty_spawn_pending;
        }
    }

    [[nodiscard]] WorkspaceMeta* selector_policy_mut(std::string selector) {
        for (WorkspaceSelectorRuleEntry& entry : g_workspace_selector_rules) {
            if (entry.selector == selector)
                return &entry.policy;
        }
        g_workspace_selector_rules.push_back(WorkspaceSelectorRuleEntry{.selector = std::move(selector), .policy = {}});
        return &g_workspace_selector_rules.back().policy;
    }

    void merge_rule_fields_into(WorkspaceMeta* meta, std::string_view key, std::string_view val, const std::string& source_name, unsigned lineno) {
        if (!meta)
            return;
        if (ascii_ieq(key, "gapsin")) {
            const auto u = wm::config::values::parse_uint_val(val);
            if (!u)
                warn_rule(source_name, lineno, u.error());
            else
                meta->rule_gaps_in = *u;
        } else if (ascii_ieq(key, "gapsout")) {
            const auto u = wm::config::values::parse_uint_val(val);
            if (!u)
                warn_rule(source_name, lineno, u.error());
            else
                meta->rule_gaps_out = *u;
        } else if (ascii_ieq(key, "bordersize")) {
            const auto u = wm::config::values::parse_uint_val(val);
            if (!u)
                warn_rule(source_name, lineno, u.error());
            else
                meta->rule_border_size = *u;
        } else if (ascii_ieq(key, "border")) {
            const auto b = wm::config::values::parse_bool_val(val);
            if (!b)
                warn_rule(source_name, lineno, b.error());
            else
                meta->rule_draw_border = *b;
        } else if (ascii_ieq(key, "monitor")) {
            meta->rule_monitor_index.assign(val.begin(), val.end());
        } else if (ascii_ieq(key, "layout")) {
            meta->rule_layout_name.assign(val.begin(), val.end());
        } else if (ascii_ieq(key, "on-created-empty")) {
            meta->on_created_empty_cmd.assign(val.begin(), val.end());
            meta->on_empty_spawn_pending = !meta->on_created_empty_cmd.empty();
        } else if (ascii_ieq(key, "default")) {
            const auto b = wm::config::values::parse_bool_val(val);
            if (!b)
                warn_rule(source_name, lineno, b.error());
            else {
                meta->rule_default = *b;
                if (*b)
                    meta->rule_default_rank = ++g_default_rank_counter;
            }
        } else if (ascii_ieq(key, "persistent")) {
            const auto b = wm::config::values::parse_bool_val(val);
            if (!b)
                warn_rule(source_name, lineno, b.error());
            else
                meta->rule_persistent = *b;
        } else if (ascii_ieq(key, "defaultName")) {
            meta->rule_default_name.assign(val.begin(), val.end());
            if (meta->rule_default_name.empty()) {
                warn_rule(source_name, lineno, "defaultName must be non-empty");
                return;
            }
            if (meta->id != 0U && (meta->name.empty() || meta->name == default_workspace_name(meta->id)))
                meta->name = meta->rule_default_name;
        } else {
            warn_rule(source_name, lineno, std::string("unknown key '").append(key).append("'"));
        }
    }
} // namespace

/* Rebuild registry from ordered workspace names (index i → id i+1). */
void workspace_registry_clear() {
    g_workspace_registry.clear();
    g_workspace_selector_rules.clear();
    g_default_rank_counter = 0U;
    special_workspace_registry_clear();
}

/* Rebuild registry entries from ordered workspace names while preserving 1-based ids. */
void workspace_registry_reset_from_ordered_names(const std::vector<std::string>& ordered_names) {
    workspace_registry_clear();

    const std::size_t total = ordered_names.size();
    g_workspace_registry.reserve(total > 0 ? total : 1U);
    for (std::size_t i = 0; i < total; ++i) {
        const WorkspaceId id   = static_cast<WorkspaceId>(i + 1U);
        std::string       name = ordered_names[i];
        if (name.empty())
            name = default_workspace_name(id);
        WorkspaceMeta meta{};
        meta.id   = id;
        meta.name = std::move(name);
        g_workspace_registry.push_back(std::move(meta));
    }
    if (g_workspace_registry.empty()) {
        WorkspaceMeta meta{};
        meta.id   = 1U;
        meta.name = default_workspace_name(1U);
        g_workspace_registry.push_back(std::move(meta));
    }
}

void workspace_registry_append_named(std::string name) {
    const WorkspaceId id = static_cast<WorkspaceId>(g_workspace_registry.size() + 1U);
    WorkspaceMeta     meta{};
    meta.id   = id;
    meta.name = std::move(name);
    g_workspace_registry.push_back(std::move(meta));
}

void workspace_registry_set_name_for_id(WorkspaceId id, std::string name) {
    if (id < kWorkspaceIdMin)
        return;
    workspace_registry_ensure_id(id);
    g_workspace_registry[static_cast<std::size_t>(id - 1U)].name = std::move(name);
}

/* Ensure a specific workspace id exists in the registry, filling intermediate defaults. */
void workspace_registry_ensure_id(WorkspaceId id) {
    if (id < kWorkspaceIdMin)
        return;
    if (g_workspace_registry.empty())
        workspace_registry_reset_from_ordered_names({});

    while (g_workspace_registry.size() < static_cast<std::size_t>(id)) {
        const WorkspaceId next_id = static_cast<WorkspaceId>(g_workspace_registry.size() + 1U);
        WorkspaceMeta     meta{};
        meta.id   = next_id;
        meta.name = default_workspace_name(next_id);
        g_workspace_registry.push_back(std::move(meta));
    }
}

/* Return the number of workspace entries currently tracked by the registry. */
std::size_t workspace_registry_count() {
    return g_workspace_registry.size();
}

/* Return registry metadata for a 0-based workspace slot used by EWMH/export paths. */
const WorkspaceMeta* workspace_registry_at(std::size_t index) {
    if (index >= g_workspace_registry.size())
        return nullptr;
    return &g_workspace_registry[index];
}

/* Resolve workspace metadata by exact configured name. */
const WorkspaceMeta* workspace_registry_find_by_name(std::string_view name) {
    for (const WorkspaceMeta& meta : g_workspace_registry) {
        if (meta.name == name)
            return &meta;
    }
    return nullptr;
}

const WorkspaceMeta* workspace_registry_find_by_id(WorkspaceId id) {
    return meta_mut_by_id(id);
}

WorkspaceMeta* workspace_registry_meta_mut(WorkspaceId id) {
    return meta_mut_by_id(id);
}

void workspace_registry_merge_rule_token(WorkspaceId id, std::string_view token, const std::string& source_name, unsigned lineno) {
    WorkspaceMeta* meta = meta_mut_by_id(id);
    if (!meta)
        return;
    const size_t colon = token.find(':');
    if (colon == std::string_view::npos) {
        warn_rule(source_name, lineno, "token must be 'key:value'");
        return;
    }
    const std::string_view key = trim_sv(token.substr(0, colon));
    const std::string_view val = trim_sv(token.substr(colon + 1));
    if (key.empty()) {
        warn_rule(source_name, lineno, "empty rule key");
        return;
    }
    merge_rule_fields_into(meta, key, val, source_name, lineno);
}

void workspace_registry_merge_selector_rule_token(const std::string_view selector, const std::string_view token, const std::string& source_name, const unsigned lineno) {
    WorkspaceMeta* meta  = selector_policy_mut(std::string(trim_sv(selector)));
    const size_t   colon = token.find(':');
    if (colon == std::string_view::npos) {
        warn_rule(source_name, lineno, "token must be 'key:value'");
        return;
    }
    const std::string_view key = trim_sv(token.substr(0, colon));
    const std::string_view val = trim_sv(token.substr(colon + 1));
    if (key.empty()) {
        warn_rule(source_name, lineno, "empty rule key");
        return;
    }
    merge_rule_fields_into(meta, key, val, source_name, lineno);
}

WorkspaceMeta workspace_registry_effective_meta(WorkspaceId id, Monitor* view_monitor) {
    WorkspaceMeta eff{};
    if (const WorkspaceMeta* base = workspace_registry_find_by_id(id)) {
        eff = *base;
    } else if (id >= kWorkspaceIdMin) {
        eff.id   = id;
        eff.name = default_workspace_name(id);
    }
    for (const WorkspaceSelectorRuleEntry& entry : g_workspace_selector_rules) {
        if (wm::workspace_selector::matches_normal_workspace(entry.selector, id, view_monitor))
            merge_policy_fields(entry.policy, eff);
    }
    return eff;
}

void workspace_registry_note_client_joined_workspace(WorkspaceId id) {
    WorkspaceMeta* meta = meta_mut_by_id(id);
    if (!meta || meta->on_created_empty_cmd.empty())
        return;
    meta->on_empty_spawn_pending = false;
}

void workspace_registry_note_workspace_became_empty(WorkspaceId id) {
    WorkspaceMeta* meta = meta_mut_by_id(id);
    if (!meta || meta->on_created_empty_cmd.empty())
        return;
    meta->on_empty_spawn_pending = true;
}

WorkspaceId workspace_registry_default_for_monitor(int monitor_num, std::string_view output_name) {
    const std::string    monitor_str = std::to_string(monitor_num);
    const WorkspaceMeta* best        = nullptr;
    for (const WorkspaceMeta& meta : g_workspace_registry) {
        if (!meta.rule_default.has_value() || !meta.rule_default.value())
            continue;
        const bool monitor_match = (meta.rule_monitor_index == monitor_str) || (!output_name.empty() && meta.rule_monitor_index == output_name);
        if (!monitor_match)
            continue;
        if (!best || meta.rule_default_rank >= best->rule_default_rank)
            best = &meta;
    }
    return best ? best->id : 0U;
}

bool workspace_registry_is_persistent(WorkspaceId id) {
    const WorkspaceMeta* meta = workspace_registry_find_by_id(id);
    if (!meta || !meta->rule_persistent.has_value())
        return true;
    return meta->rule_persistent.value();
}
