/* Parse niri-style `window-rule { ... }` blocks into `WindowRuleEntry` list.
 *
 * Role:
 * - Convert textual rule clauses into typed `WindowRuleEntry` structures.
 *
 * Contract:
 * - Throws contextual parse errors for malformed clauses or invalid arguments.
 * - Appends parsed entries into global rule registry (`window_rules`).
 *
 * Scope:
 * - Targets niri-style window-rule syntax adopted by this project.
 * - Does not execute effects; only parses and stores rule metadata.
 */
#pragma once

#include "config.hpp"

#include <istream>
#include <string_view>

namespace wm::config::parse {

    /* Parse a `window-rule { ... }` block body until closing `}`. */
    void parse_window_rule_block(std::istream& in, unsigned& lineno, std::string_view source_name, const ConfVars& conf_vars);

} /* namespace wm::config::parse */
