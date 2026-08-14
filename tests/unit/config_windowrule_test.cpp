/* Unit tests for niri-style window-rule parser and open-time apply behavior. */
#include "config/parse/window_rule.hpp"
#include "config/parse/window_rule_parse.hpp"
#include "monitor_select.hpp"
#include "workspace_registry.hpp"
#include "special_workspace_registry.hpp"
#include "types.hpp"
#include "wm_state.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <sstream>
#include <string>

/* Minimal link stubs for symbols not exercised by this test scenario. */
const WorkspaceMeta* workspace_registry_find_by_name(std::string_view) {
    return nullptr;
}

std::optional<std::uint8_t> special_workspace_registry_ensure_tag(std::string) {
    return std::nullopt;
}

std::optional<WorkspaceId> special_workspace_registry_hidden_id_by_tag(std::string_view) {
    return std::nullopt;
}

WorkspaceRef workspace_normalize_special_ref_with_hidden_id(const WorkspaceRef& ws, std::optional<WorkspaceId>) {
    return ws;
}

namespace {
    Monitor* g_monitor_resolve_result = nullptr;
}

Monitor* monitor_select_resolve(std::string_view) {
    return g_monitor_resolve_result;
}

static bool expect_true(bool cond, const char* case_name) {
    if (cond)
        return true;
    fprintf(stderr, "windowrule case failed: %s\n", case_name);
    return false;
}

static bool test_parse_window_rule_block_basic() {
    wm::config::parse::clear_window_rules();
    const ConfVars     vars;
    std::istringstream in("match app-id=\"^firefox$\" title=\"Gmail\"\n"
                          "exclude app-id=\"^firefox$\" title=\"Media viewer\"\n"
                          "open-floating=true\n"
                          "size=80% 50%\n"
                          "move=10% 20%\n"
                          "center=true\n"
                          "open-fullscreen=false\n"
                          "open-on-output=\"DP-1\"\n"
                          "}\n");
    unsigned           line = 0U;
    wm::config::parse::parse_window_rule_block(in, line, "<unit>", vars);
    if (!expect_true(wm::config::parse::window_rules.size() == 1U, "one rule parsed"))
        return false;
    const auto& e = wm::config::parse::window_rules.front();
    if (!expect_true(e.matches.size() == 1U, "one match directive"))
        return false;
    if (!expect_true(e.excludes.size() == 1U, "one exclude directive"))
        return false;
    if (!expect_true(e.effect_open_floating.has_value() && *e.effect_open_floating, "open-floating parsed"))
        return false;
    if (!expect_true(e.effect_open_size.has_value() && e.effect_open_size->first.percent && e.effect_open_size->second.percent, "size parsed as percent"))
        return false;
    if (!expect_true(e.effect_open_move.has_value() && e.effect_open_move->first.percent && e.effect_open_move->second.percent, "move parsed as percent"))
        return false;
    if (!expect_true(e.effect_open_center.has_value() && *e.effect_open_center, "center parsed"))
        return false;
    if (!expect_true(e.effect_open_fullscreen.has_value() && !*e.effect_open_fullscreen, "open-fullscreen parsed"))
        return false;
    if (!expect_true(e.effect_open_on_output.has_value() && *e.effect_open_on_output == "DP-1", "open-on-output parsed"))
        return false;
    return true;
}

static bool test_match_directive_accepts_spaced_equals() {
    wm::config::parse::clear_window_rules();
    const ConfVars     vars;
    std::istringstream in("match app-id = terminal-dropdown title = \"dropdown term\"\n"
                          "open-floating=true\n"
                          "}\n");
    unsigned           line = 0U;
    wm::config::parse::parse_window_rule_block(in, line, "<unit>", vars);
    if (!expect_true(wm::config::parse::window_rules.size() == 1U, "spaced equals rule parsed"))
        return false;
    wm::config::parse::WindowRuleEntry e = wm::config::parse::window_rules.front();
    Client                             c{};
    c.name = "dropdown term";
    if (!expect_true(wm::config::parse::window_rule_entry_matches(e, &c, "terminal-dropdown"), "spaced equals matcher works"))
        return false;
    return true;
}

static bool test_rule_matching_match_and_exclude() {
    wm::config::parse::WindowRuleEntry            e{};
    wm::config::parse::WindowRuleMatcherDirective m{};
    wm::config::parse::WindowRuleMatcherDirective ex{};
    m.app_id = std::regex("^firefox$");
    ex.title = std::regex("^Media viewer$");
    e.matches.push_back(m);
    e.excludes.push_back(ex);
    Client c{};
    c.name = "Main";
    if (!expect_true(wm::config::parse::window_rule_entry_matches(e, &c, "firefox"), "base match passes"))
        return false;
    c.name = "Media viewer";
    if (!expect_true(!wm::config::parse::window_rule_entry_matches(e, &c, "firefox"), "exclude blocks"))
        return false;
    return true;
}

/* Validate output selector effect updates client monitor when resolution succeeds. */
static bool test_apply_output_selector_effect_sets_monitor_on_resolve() {
    wm::config::parse::WindowRuleEntry e{};
    Client                             c{};
    Monitor                            current{};
    Monitor                            resolved{};

    c.mon                    = &current;
    e.effect_open_on_output  = "DP-1";
    g_monitor_resolve_result = &resolved;

    wm::config::parse::window_rule_apply_prestack(e, &c);
    if (!expect_true(c.mon == &resolved, "output effect: resolved selector updates c.mon"))
        return false;

    return true;
}

/* Validate output selector effect keeps monitor unchanged when resolution fails. */
static bool test_apply_output_selector_effect_keeps_monitor_on_unresolved() {
    wm::config::parse::WindowRuleEntry e{};
    Client                             c{};
    Monitor                            current{};

    c.mon                    = &current;
    e.effect_open_on_output  = "DP-1";
    g_monitor_resolve_result = nullptr;

    wm::config::parse::window_rule_apply_prestack(e, &c);
    if (!expect_true(c.mon == &current, "output effect: unresolved selector keeps existing c.mon"))
        return false;

    return true;
}

static bool test_apply_open_effects() {
    wm::config::parse::WindowRuleEntry e{};
    Client                             c{};
    Monitor                            m{};
    m.ww                   = 1000;
    m.wh                   = 800;
    m.wx                   = 50;
    m.wy                   = 25;
    c.mon                  = &m;
    e.effect_open_floating = true;
    e.effect_open_focused  = false;
    e.effect_open_size =
        std::make_pair(wm::config::parse::WindowRuleAxisSpan{.percent = true, .value = 50.0}, wm::config::parse::WindowRuleAxisSpan{.percent = false, .value = 777.0});
    e.effect_open_move =
        std::make_pair(wm::config::parse::WindowRuleAxisSpan{.percent = true, .value = 10.0}, wm::config::parse::WindowRuleAxisSpan{.percent = false, .value = 33.0});
    e.effect_open_center     = true;
    e.effect_open_fullscreen = true;
    wm::config::parse::window_rule_apply_prestack(e, &c);
    if (!expect_true(c.isfloating == 1, "open-floating true sets floating"))
        return false;
    if (!expect_true(c.neverfocus == 1, "open-focused false sets neverfocus"))
        return false;
    if (!expect_true(c.w == 500 && c.h == 777, "size supports percent and px"))
        return false;
    if (!expect_true(c.x == 150 && c.y == 33, "move supports percent and px"))
        return false;
    if (!expect_true(c.rule_center_pending == 2U, "center true sets pending center"))
        return false;
    if (!expect_true(c.rule_fullscreen_pending == 2U, "open-fullscreen true sets pending on"))
        return false;
    return true;
}

int main() {
    if (!test_parse_window_rule_block_basic())
        return EXIT_FAILURE;
    if (!test_match_directive_accepts_spaced_equals())
        return EXIT_FAILURE;
    if (!test_rule_matching_match_and_exclude())
        return EXIT_FAILURE;
    if (!test_apply_output_selector_effect_sets_monitor_on_resolve())
        return EXIT_FAILURE;
    if (!test_apply_output_selector_effect_keeps_monitor_on_unresolved())
        return EXIT_FAILURE;
    if (!test_apply_open_effects())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
