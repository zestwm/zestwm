/* Config parser string helpers shared across config modules.
 *
 * Scope:
 * - Stateless token/line normalization helpers used by all parser submodules.
 *
 * Design:
 * - Helpers are allocation-light and deterministic.
 * - Semantics intentionally match current config grammar behavior (no shell
 *   parser emulation beyond basic quote/comment handling).
 */
#pragma once

#include <string>
#include <string_view>

namespace wm::config::parse {

    /* Trim leading/trailing ASCII whitespace from config tokens.
     *
     * Contract:
     * - Returns empty string for all-whitespace input.
     * - Whitespace definition matches parser tokenization helpers.
     * - Does not alter inner spacing.
     */
    [[nodiscard]] std::string trim(std::string_view s);

    /* Remove one pair of surrounding double quotes, then trim.
     *
     * Contract:
     * - Removes exactly one outer quote pair when both ends are `"`.
     * - Leaves inner/escaped quotes untouched.
     * - Returns already-trimmed output to simplify downstream parsing.
     */
    [[nodiscard]] std::string strip_outer_quotes(std::string s);

    /* Strip shell-style comments while preserving quoted and hex-color tokens.
     *
     * Rules:
     * - `#` inside single/double quoted segments is preserved.
     * - Hex color literals like `#fff`, `#AABBCC`, `#AARRGGBB` are preserved.
     * - Trailing content starting from a real comment marker is removed.
     *
     * Non-goals:
     * - Full shell escaping semantics.
     * - Multi-line quote recovery (line-local operation only).
     */
    [[nodiscard]] std::string strip_line_comment(std::string line);

} /* namespace wm::config::parse */
