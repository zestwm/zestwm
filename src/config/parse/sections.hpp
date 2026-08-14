/* Section block parser entry points for misc/general/dwindle blocks.
 *
 * Role:
 * - Parse top-level config blocks with per-section validation rules.
 * - Normalize per-block key handling before values flow into runtime globals.
 *
 * Contract:
 * - Throws contextual parse errors for malformed block structure.
 * - Applies known keys directly to current runtime configuration globals.
 * - Preserves section-specific behavior for unknown keys:
 *   - `misc` / `dwindle`: hard error
 *   - `general` / `general:snap`: warn-and-ignore for unsupported X11 keys
 *
 * Architectural notes:
 * - Parsers currently mutate global runtime config values.
 * - Execution model is startup single-threaded; concurrent reload requires
 *   external synchronization or state handoff.
 * - `general` compatibility keys intentionally remain non-fatal to preserve
 *   cross-backend config portability.
 */
#pragma once

#include <istream>

namespace wm::config::parse::sections {

    /* Parse `misc { ... }` and apply strictly validated misc runtime settings.
     *
     * Unknown-key policy: throw.
     */
    void parse_misc_block(std::istream& in, unsigned int& lineno, const char* path);

    /* Parse `general { ... }` including optional nested `snap { ... }`.
     *
     * Unknown-key policy: warn and ignore for known unsupported/compat keys.
     */
    void parse_general_block(std::istream& in, unsigned int& lineno, const char* path);

    /* Parse `dwindle { ... }` and update split-behavior defaults.
     *
     * Unknown-key policy: throw.
     */
    void parse_dwindle_block(std::istream& in, unsigned int& lineno, const char* path);

} /* namespace wm::config::parse::sections */
