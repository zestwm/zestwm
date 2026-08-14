#pragma once

/* Registry of special workspace tags (`special:<tag>`), separate from normal numeric workspaces.
 *
 * Responsibilities:
 * - maintain stable tag -> slot mapping for configured special workspaces;
 * - expose bridge conversion between special tags and hidden workspace ids used by runtime state;
 * - store per-tag metadata parsed from `workspace = special:<tag>, key:value` policy tokens.
 *
 * Lifecycle:
 * - registry is cleared whenever normal workspace registry is reset (`wmconf_load`/`wmconf_free`);
 * - tags are repopulated from the current config only;
 * - persisted clients referring to dropped tags are re-routed by higher-level restore/adopt_client logic.
 *
 * Limits:
 * - maximum 97 special tags (product/runtime rule aligned with hidden-id bridge space). */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "workspace_id.hpp"

struct SpecialWorkspaceMeta {
    std::string tag;
    /* Per-tag `dim_special` override when overlay is open; unset uses global `dim_special`. */
    std::optional<double> rule_dim_special;
};

/* Clear all special tag slots and metadata. */
void special_workspace_registry_clear();

/* Register tag if missing; returns stable slot 0..96. Empty tag is allowed (default special). std::nullopt when at cap (logs a warning). */
[[nodiscard]] std::optional<std::uint8_t> special_workspace_registry_ensure_tag(std::string tag);

/* Slot for an already-registered tag only (no insert). Used by BSP storage keyed by registry index. */
[[nodiscard]] std::optional<std::uint8_t> special_workspace_registry_slot_by_tag(std::string_view tag);

/* Stable hidden workspace id for a registered tag: contiguous bridge space (`32 + slot`). */
[[nodiscard]] std::optional<WorkspaceId> special_workspace_registry_hidden_id_by_tag(std::string_view tag);

/* Reverse mapping for hidden workspace id used by special tags. */
[[nodiscard]] std::optional<std::uint8_t> special_workspace_registry_slot_by_hidden_id(WorkspaceId hidden_id);

/* Lookup metadata for a registered tag; nullptr when unknown. */
[[nodiscard]] const SpecialWorkspaceMeta* special_workspace_registry_find_by_tag(std::string_view tag);

/* Number of currently registered special tags. */
[[nodiscard]] std::size_t special_workspace_registry_count();

/* Metadata by slot index; nullptr when out of range. */
[[nodiscard]] const SpecialWorkspaceMeta* special_workspace_registry_at(std::size_t index);

/* Merge one `key:value` token from `workspace = special:<tag>, ...` (unknown keys log a warning). */
void special_workspace_registry_merge_rule_token(std::string_view tag, std::string_view token, const std::string& source_name, unsigned lineno);
