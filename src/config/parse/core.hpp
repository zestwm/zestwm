/* Core config parser orchestration entry points.
 *
 * Role:
 * - Own top-level line scanning, include resolution, and directive dispatch.
 *
 * Scope:
 * - Delegates block/body parsing to specialized modules (`sections`, `bindings`,
 *   `workspace`, `window_rule_parse`, etc.).
 * - Maintains parser diagnostics context and include-cycle guards.
 */
#pragma once

#include "config.hpp"

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace wm::config::parse::core {

    /* Mutable parser runtime context shared across core stream/file parsing.
     *
     * `loaded_file_set` provides O(1) average duplicate suppression while
     * `loaded_file_list` preserves first-seen include order for reporting/UI.
     * Both containers are updated together by core include-tracking helpers.
     *
     * Threading:
     * - Startup parsing is single-threaded by design.
     * - If parser reload is ever executed from multiple threads, callers must
     *   provide external synchronization around the full `CoreContext`.
     */
    struct CoreContext {
        ConfigState&                     config;
        std::vector<std::string>&        loaded_file_list;
        std::unordered_set<std::string>& loaded_file_set;
        std::string&                     error_notice;
        unsigned int&                    error_count;
    };

    /* Parse one already-open stream and dispatch top-level config directives.
     *
     * Contract:
     * - Recovers from per-line parse exceptions and continues scanning.
     * - Uses `include_stack`/`depth` for nested source-tracking consistency.
     */
    void parse_config_stream(CoreContext& ctx, std::istream& in, const std::filesystem::path& base_dir, std::string_view source_name, unsigned depth,
                             std::vector<std::string>& include_stack);

    /* Parse a config file with canonical path normalization and cycle detection.
     *
     * Contract:
     * - Throws on path resolution/open errors and include-cycle violations.
     * - Updates `loaded_file_set`/`loaded_file_list` together:
     *   deduplicated membership + first-seen order.
     */
    void parse_config_file(CoreContext& ctx, const std::filesystem::path& file_path, unsigned depth, std::vector<std::string>& include_stack);

} /* namespace wm::config::parse::core */
