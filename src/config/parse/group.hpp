/* Config parser entry point for group/groupbar section.
 *
 * Role:
 * - Parse `group { ... }` including nested `groupbar { ... }`.
 * - Map accepted keys to current runtime globals used by group/groupbar logic.
 *
 * Contract:
 * - Throws contextual parse errors for malformed structure or invalid values.
 * - Applies known keys directly to runtime group/groupbar configuration.
 * - Unknown keys are ignored with warnings to preserve forward compatibility.
 *
 * Architectural notes:
 * - Current implementation mutates global runtime config variables.
 * - Parsing is startup single-threaded; concurrent reload requires external
 *   synchronization or copy-on-write config state handoff.
 * - Some keys are intentionally no-op on X11 or reserved for future behavior;
 *   implementations should keep explicit rationale comments near those branches.
 *
 * Migration direction (future):
 * - Route writes through an explicit config state object (e.g. `ConfigState&`)
 *   to improve isolated testing and reload safety.
 *
 * Maintenance note:
 * - Key/value micro-helpers may appear duplicated across parser modules.
 *   This is currently intentional to keep error wording contextual per section.
 */
#pragma once

#include <istream>

namespace wm::config::parse {

    /* Parse group { ... } block including nested groupbar { ... } settings. */
    void parse_group_block(std::istream& in, unsigned int& lineno, const char* path);

} /* namespace wm::config::parse */
