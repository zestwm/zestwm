/* Config block parser entry points for nested sections.
 *
 * Role:
 * - Parse structured `input { ... }` and nested `device { ... }` sections.
 *
 * Scope:
 * - Validation + assignment into runtime config state only.
 * - No side effects outside parser-owned config structures.
 */
#pragma once

#include <istream>

namespace wm::config::parse {

    /* Parse `input { ... }` block and update global input config state.
     *
     * Contract:
     * - Consumes lines from `in` until matching `}` for the input block.
     * - Throws parser errors with path/line context on malformed content.
     */
    void parse_input_block(std::istream& in, unsigned int& lineno, const char* path);

    /* Parse `device { ... }` sub-block and append one device override entry.
     *
     * Contract:
     * - Requires caller to be inside an `input` block.
     * - Throws parser errors on invalid keys, values, or unterminated blocks.
     */
    void parse_device_block(std::istream& in, unsigned int& lineno, const char* path);

} /* namespace wm::config::parse */
