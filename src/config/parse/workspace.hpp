/* Workspace directive parser for top-level `workspace = ...` lines.
 *
 * Responsibilities:
 * - parse selector forms (`<id>`, `<name>`, `name:<name>`, `special:<tag>`);
 * - expand variables/quotes before interpretation;
 * - update normal/special workspace registries and per-workspace rule metadata.
 *
 * Scope:
 * - this module only handles the top-level workspace directive grammar;
 * - runtime routing/visibility semantics live in workspace/runtime modules.
 *
 * Threading:
 * - intended for startup/reload parsing on a single thread. */
#pragma once

#include "config.hpp"

#include <string_view>

namespace wm::config::parse::workspace {

    /* Parse one top-level `workspace = ...` line and update workspace registries.
     *
     * Accepted selector forms:
     * - `N`                      -> ensure normal workspace id N exists;
     * - `<name>` / `name:<name>` -> create/resolve normal workspace by display name;
     * - `special:<tag>`          -> create/resolve special workspace tag (empty tag allowed).
     *
     * Accepted trailing tokens:
     * - normal workspaces: optional display name + known `key:value` workspace policy rules;
     * - special workspaces: only `key:value` special-policy rules.
     *
     * Error model:
     * - throws `std::runtime_error` on malformed directives;
     * - error text is source-qualified (`<source>:<line>: ...`) for config diagnostics. */
    void apply_workspace_line(std::string_view raw_value, ConfVars& conf_vars, std::string_view source_name, unsigned lineno);

} /* namespace wm::config::parse::workspace */
