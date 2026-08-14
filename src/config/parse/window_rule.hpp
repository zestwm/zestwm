/* Niri-style window rule storage + runtime application contracts.
 * Parsing is implemented in `window_rule_parse.cpp` (`window-rule { ... }` blocks).
 *
 * Responsibilities:
 * - define in-memory shape for one parsed rule (`WindowRuleEntry`);
 * - expose process-wide registry used by adopt_client-time rule evaluation;
 * - declare match/apply entry points used by client adopt_client flow.
 *
 * Scope:
 * - targets niri-compatible `window-rule` model adapted to X11 capabilities;
 * - unsupported compositor-only properties are rejected by parser.
 *
 * Threading model:
 * - parser mutation is startup/reload single-threaded;
 * - runtime reads happen on WM main thread in adopt_client/event handling paths. */
#pragma once

#include "workspace_ref.hpp"

#include <optional>
#include <regex>
#include <string>
#include <vector>

struct Client;

namespace wm::config::parse {

    /* Numeric span token used by size/move parser:
     * - `percent=false`: absolute px value.
     * - `percent=true`: percent of monitor work area axis. */
    struct WindowRuleAxisSpan {
        bool   percent{};
        double value{};
    };

    /* One match/exclude directive for niri-style `window-rule`.
     * Matchers inside one directive are conjunctive (AND). */
    struct WindowRuleMatcherDirective {
        std::optional<std::regex> title;
        std::optional<std::regex> app_id;
        std::optional<bool>       is_active;
        std::optional<bool>       is_focused;
        std::optional<bool>       is_floating;
        std::optional<bool>       is_urgent;
    };

    /* One parsed `window-rule` entry.
     * Rule applies when any `match` directive matches and no `exclude` directive matches. */
    struct WindowRuleEntry {
        std::vector<WindowRuleMatcherDirective> matches;
        std::vector<WindowRuleMatcherDirective> excludes;

        /* Open-time effects. */
        std::optional<bool>                                              effect_open_floating;
        std::optional<bool>                                              effect_open_focused;
        std::optional<bool>                                              effect_open_fullscreen;
        std::optional<bool>                                              effect_open_maximized;
        std::optional<std::pair<WindowRuleAxisSpan, WindowRuleAxisSpan>> effect_open_size; /* width, height (px or %) */
        std::optional<std::pair<WindowRuleAxisSpan, WindowRuleAxisSpan>> effect_open_move; /* x, y (px or %) */
        std::optional<bool>                                              effect_open_center;
        std::optional<WorkspaceRef>                                      effect_workspace_target;
        bool                                                             effect_workspace_silent = false; /* extension: keep existing `silent` route semantics */
        std::optional<std::string>                                       effect_open_on_output;
    };

    /* Runtime rule store preserving config order.
     * Entries are append-only during parse and cleared on config reload. */
    extern std::vector<WindowRuleEntry> window_rules;

    /* Clear global rule registry (reload/reset path). */
    void clear_window_rules();

    /* Return true when one `match` directive matches and no `exclude` directive matches.
     * - pure predicate (no side effects);
     * - `class_name` null pointers are treated as empty strings for regex safety. */
    [[nodiscard]] bool window_rule_entry_matches(const WindowRuleEntry& e, const Client* c, const char* class_name);

    /* Apply open-time side effects before tree insertion.
     * - mutates `Client` fields only (plus monitor pointer routing for pre-stack placement);
     * - does not insert into BSP tree, does not restack, does not trigger arrange directly. */
    void window_rule_apply_prestack(const WindowRuleEntry& e, Client* c);

} /* namespace wm::config::parse */
