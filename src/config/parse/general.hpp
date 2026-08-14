/* General option parser helpers shared by core and section parsing.
 *
 * Architectural notes:
 * - Current implementation writes runtime globals (e.g. `g_config.borderpx`, `g_config.modkey`).
 * - Config parsing is cold-path startup logic and currently exception-based.
 * - Parsing is single-threaded; callers must add external synchronization before
 *   enabling concurrent/dynamic reload from worker threads.
 *
 * Migration direction (future):
 * - For higher testability/reload isolation, route writes through a dedicated
 *   config state object instead of direct global mutation.
 * - If exceptions are ever disabled (`-fno-exceptions`), migrate parser
 *   contracts to explicit result propagation (e.g. `std::expected`).
 */
#pragma once

#include "types.hpp"

#include <string_view>

namespace wm::config::parse::general {

    /* Resolve runtime layout callback from config layout name. */
    [[nodiscard]] void (*layout_by_name(std::string_view name))(Monitor*);

    /* Like layout_by_name but returns nullptr for unknown names (workspace rules use soft failure). */
    [[nodiscard]] void (*try_layout_by_name(std::string_view name) noexcept)(Monitor*);

    /* Apply one top-level general key/value directive to runtime config state. */
    void apply_general(std::string_view key, std::string_view val, const char* path, unsigned lineno);

} /* namespace wm::config::parse::general */
