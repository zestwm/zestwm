#pragma once

#include "workspace_id.hpp"

struct Monitor;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/* Per-workspace policy parsed from `workspace = ..., key:value` directives. */
struct WorkspaceMeta {
    WorkspaceId id{};
    std::string name;
    /* Inner gap between tiled splits; unset = use global `gaps_in`. */
    std::optional<unsigned> rule_gaps_in;
    /* Outer inset from monitor work area; unset falls back to inner override then global `gaps_in`. */
    std::optional<unsigned> rule_gaps_out;
    /* Override client border width for clients on this workspace (see adopt_client / movetoworkspace). */
    std::optional<unsigned> rule_border_size;
    /* false = force zero border width while tiled on this workspace. */
    std::optional<bool> rule_draw_border;
    /* Monitor selector (`id` or output name like `DP-1`) used when viewing this workspace. */
    std::string rule_monitor_index;
    /* true marks workspace as default target for its bound monitor. */
    std::optional<bool> rule_default;
    /* Keep workspace tree state when empty/inactive; default behavior keeps it. */
    std::optional<bool> rule_persistent;
    /* Optional fallback display name for numeric/default-named workspaces. */
    std::string rule_default_name;
    /* Parse-order rank for default selection; higher wins (last declaration wins). */
    std::uint64_t rule_default_rank = 0U;
    /* Tiling layout name for the primary layout slot when this workspace is shown (`tree`, `dwindle`, `monocle`). */
    std::string rule_layout_name;
    /* Shell command run when the workspace is shown with zero clients (see view_workspace_id). */
    std::string on_created_empty_cmd;
    /* When true, next empty view may spawn `on_created_empty_cmd` (armed on parse and when last client leaves). */
    bool on_empty_spawn_pending = false;
};

/* Clear all registry entries before a config/runtime rebuild. */
void workspace_registry_clear();

/* Seed registry from ordered workspace names (index i → workspace id i+1). */
void workspace_registry_reset_from_ordered_names(const std::vector<std::string>& ordered_names);

/* Append next workspace id with the given display name (sequential `workspace = name` lines). */
void workspace_registry_append_named(std::string name);

/* Ensure ids 1..id exist, then set the name for workspace id (explicit `workspace = id, name`). */
void workspace_registry_set_name_for_id(WorkspaceId id, std::string name);

/* Ensure workspace id exists in registry (creates default names for gaps). */
void workspace_registry_ensure_id(WorkspaceId id);

/* Number of workspaces currently tracked by the registry. */
[[nodiscard]] std::size_t workspace_registry_count();

/* Return workspace metadata by 0-based index, nullptr when out of range. */
[[nodiscard]] const WorkspaceMeta* workspace_registry_at(std::size_t index);

/* Resolve workspace id by exact name, nullptr when not found. */
[[nodiscard]] const WorkspaceMeta* workspace_registry_find_by_name(std::string_view name);

/* Resolve workspace metadata by id (1-based), nullptr when id out of range or registry empty. */
[[nodiscard]] const WorkspaceMeta* workspace_registry_find_by_id(WorkspaceId id);

/* Mutable metadata for runtime hooks (empty-command arming, etc.). */
[[nodiscard]] WorkspaceMeta* workspace_registry_meta_mut(WorkspaceId id);

/* Parse one `key:value` workspace rule token and merge into workspace id; emits warnings for unknown keys. */
void workspace_registry_merge_rule_token(WorkspaceId id, std::string_view token, const std::string& source_name, unsigned lineno);

/* Merge one `key:value` token into a dynamic selector rule entry (`workspace = r[...], ...`). */
void workspace_registry_merge_selector_rule_token(std::string_view selector, std::string_view token, const std::string& source_name, unsigned lineno);

/* Resolve per-id metadata merged with matching dynamic selector rules for `view_monitor`. */
[[nodiscard]] WorkspaceMeta workspace_registry_effective_meta(WorkspaceId id, Monitor* view_monitor);

/* Called when a client is mapped or moved onto this workspace so empty-workspace hooks stay coherent. */
void workspace_registry_note_client_joined_workspace(WorkspaceId id);

/* Called when the last client leaves this workspace (release_client path) to re-arm on-created-empty. */
void workspace_registry_note_workspace_became_empty(WorkspaceId id);

/* Resolve default workspace id for monitor (`Monitor.num` and optional RandR output name), 0 when none configured. */
[[nodiscard]] WorkspaceId workspace_registry_default_for_monitor(int monitor_num, std::string_view output_name);

/* Return whether workspace should keep state when empty/inactive (default true). */
[[nodiscard]] bool workspace_registry_is_persistent(WorkspaceId id);
