#pragma once

/* Dynamic workspace selector parser/matcher for `workspace = SELECTOR, RULES` config lines. */

#include "workspace_id.hpp"

struct Monitor;

namespace wm::workspace_selector {

    /* True when the workspace head token uses bracket selectors (`r[...]`, `w[...]`, ...). */
    [[nodiscard]] bool looks_like_workspace_selector(std::string_view head) noexcept;

    /* Match a normal desktop id against a selector string (AND across clauses). */
    [[nodiscard]] bool matches_normal_workspace(std::string_view selector, WorkspaceId id, Monitor* view_monitor) noexcept;

} /* namespace wm::workspace_selector */
