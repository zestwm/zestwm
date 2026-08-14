/* Unit tests for top-level workspace directive parser (`workspace = ...`). */
#include "config/parse/workspace.hpp"
#include "special_workspace_registry.hpp"
#include "workspace_registry.hpp"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <stdexcept>
#include <string>

static bool expect_true(bool cond, const char* case_name) {
    if (cond)
        return true;
    fprintf(stderr, "workspace parser case failed: %s\n", case_name);
    return false;
}

static bool test_numeric_selector_with_name_and_rule() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("3, ws3, monitor:DP-1", vars, "<unit>", 1U);

    const WorkspaceMeta* meta = workspace_registry_find_by_id(3U);
    if (!expect_true(meta != nullptr, "numeric selector creates id 3"))
        return false;
    if (!expect_true(meta->name == "ws3", "display name assigned"))
        return false;
    if (!expect_true(meta->rule_monitor_index == "DP-1", "monitor rule parsed"))
        return false;
    return true;
}

static bool test_special_selector_with_rule() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("special:pad, dim_special:0.7", vars, "<unit>", 1U);

    const SpecialWorkspaceMeta* meta = special_workspace_registry_find_by_tag("pad");
    if (!expect_true(meta != nullptr, "special tag registered"))
        return false;
    if (!expect_true(meta->rule_dim_special.has_value(), "special rule dim_special parsed"))
        return false;
    if (!expect_true(fabs(*meta->rule_dim_special - 0.7) < 1e-9, "dim_special numeric value stored"))
        return false;
    return true;
}

static bool test_name_selector_expands_variable() {
    workspace_registry_clear();
    ConfVars vars;
    vars["WS_NAME"] = "dev";

    wm::config::parse::workspace::apply_workspace_line("name:$WS_NAME", vars, "<unit>", 1U);

    const WorkspaceMeta* meta = workspace_registry_find_by_name("dev");
    if (!expect_true(meta != nullptr, "name selector with variable expansion"))
        return false;
    return true;
}

static bool test_multiple_non_rule_tokens_throw() {
    workspace_registry_clear();
    ConfVars vars;

    try {
        wm::config::parse::workspace::apply_workspace_line("2, foo, bar", vars, "<unit>", 1U);
        return expect_true(false, "multiple non-rule tokens must throw");
    } catch (const std::runtime_error&) { return true; }
}

static bool test_empty_name_selector_throws() {
    workspace_registry_clear();
    ConfVars vars;

    try {
        wm::config::parse::workspace::apply_workspace_line("name:   ", vars, "<unit>", 1U);
        return expect_true(false, "empty name selector must throw");
    } catch (const std::runtime_error&) { return true; }
}

static bool test_display_name_with_colon_is_not_misclassified_as_rule() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("2, my:workspace, monitor:DP-1", vars, "<unit>", 1U);

    const WorkspaceMeta* meta = workspace_registry_find_by_id(2U);
    if (!expect_true(meta != nullptr, "id 2 exists"))
        return false;
    if (!expect_true(meta->name == "my:workspace", "colon token treated as display name"))
        return false;
    if (!expect_true(meta->rule_monitor_index == "DP-1", "monitor rule still applied"))
        return false;
    return true;
}

static bool test_quoted_comma_display_name_is_parsed_as_one_token() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("2, \"my,workspace\", monitor:DP-1", vars, "<unit>", 1U);

    const WorkspaceMeta* meta = workspace_registry_find_by_id(2U);
    if (!expect_true(meta != nullptr, "id 2 exists for quoted comma name"))
        return false;
    if (!expect_true(meta->name == "my,workspace", "quoted comma preserved in display name"))
        return false;
    if (!expect_true(meta->rule_monitor_index == "DP-1", "rule after quoted comma still parsed"))
        return false;
    return true;
}

static bool test_unclosed_quote_throws() {
    workspace_registry_clear();
    ConfVars vars;

    try {
        wm::config::parse::workspace::apply_workspace_line("2, \"my,workspace, monitor:DP-1", vars, "<unit>", 1U);
        return expect_true(false, "unclosed quote must throw");
    } catch (const std::runtime_error&) { return true; }
}

/* Registry-first name lookup used by bind/workspace action parsing (MUST-PARSER-001). */
static bool test_display_name_findable_by_registry_name() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("7, WmstateNamedWs", vars, "<unit>", 1U);

    const WorkspaceMeta* by_name = workspace_registry_find_by_name("WmstateNamedWs");
    if (!expect_true(by_name != nullptr, "workspace_registry_find_by_name"))
        return false;
    if (!expect_true(by_name->id == 7U, "named workspace resolves to id 7"))
        return false;
    if (!expect_true(workspace_registry_find_by_id(7U) == by_name, "id lookup aliases same meta"))
        return false;
    return true;
}

/* `workspace = <name>` appends a new registry row when the name is unseen. */
static bool test_bare_name_appends_workspace() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("alpha", vars, "<unit>", 1U);

    const WorkspaceMeta* meta = workspace_registry_find_by_name("alpha");
    if (!expect_true(meta != nullptr, "bare name creates workspace"))
        return false;
    if (!expect_true(meta->id == 1U, "first append gets id 1"))
        return false;
    if (!expect_true(workspace_registry_count() == 1U, "registry grows by one"))
        return false;
    return true;
}

/* Re-declaring the same display name is a no-op (docs/config/workspaces.md). */
static bool test_bare_name_redundant_is_noop() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("alpha", vars, "<unit>", 1U);
    wm::config::parse::workspace::apply_workspace_line("alpha", vars, "<unit>", 2U);

    if (!expect_true(workspace_registry_count() == 1U, "duplicate bare name does not grow registry"))
        return false;
    const WorkspaceMeta* meta = workspace_registry_find_by_name("alpha");
    if (!expect_true(meta != nullptr && meta->id == 1U, "duplicate bare name keeps original id"))
        return false;
    return true;
}

/* `workspace = <id>` ensures sparse ids and fills intermediate default names. */
static bool test_numeric_id_only_fills_sparse_defaults() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("5", vars, "<unit>", 1U);

    if (!expect_true(workspace_registry_count() == 5U, "sparse id extends registry to target id"))
        return false;
    for (WorkspaceId id = 1U; id <= 5U; ++id) {
        const WorkspaceMeta* meta = workspace_registry_find_by_id(id);
        if (!expect_true(meta != nullptr, "sparse fill creates every intermediate id"))
            return false;
        const std::string expected = std::to_string(static_cast<unsigned>(id));
        if (!expect_true(meta->name == expected, "gap ids keep default numeric names"))
            return false;
    }
    return true;
}

/* `workspace = <id>, <name>` overrides the default name for that id only. */
static bool test_numeric_id_with_name_overrides_default() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("4, dev", vars, "<unit>", 1U);

    if (!expect_true(workspace_registry_count() == 4U, "id+name extends registry"))
        return false;
    const WorkspaceMeta* id3 = workspace_registry_find_by_id(3U);
    if (!expect_true(id3 != nullptr && id3->name == "3", "unspecified gap id keeps default name"))
        return false;
    const WorkspaceMeta* id4 = workspace_registry_find_by_id(4U);
    if (!expect_true(id4 != nullptr && id4->name == "dev", "target id gets explicit display name"))
        return false;
    return true;
}

/* Workspace policy keys merge into registry metadata. */
static bool test_workspace_rule_keys_merge() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("2, gapsin:8, gapsout:12, bordersize:3, border:false, layout:monocle, persistent:false, default:true, "
                                                       "defaultName:media, on-created-empty:foot",
                                                       vars, "<unit>", 1U);

    WorkspaceMeta* meta = workspace_registry_meta_mut(2U);
    if (!expect_true(meta != nullptr, "rule merge target exists"))
        return false;
    if (!expect_true(meta->rule_gaps_in.has_value() && *meta->rule_gaps_in == 8U, "gapsin"))
        return false;
    if (!expect_true(meta->rule_gaps_out.has_value() && *meta->rule_gaps_out == 12U, "gapsout"))
        return false;
    if (!expect_true(meta->rule_border_size.has_value() && *meta->rule_border_size == 3U, "bordersize"))
        return false;
    if (!expect_true(meta->rule_draw_border.has_value() && !meta->rule_draw_border.value(), "border:false"))
        return false;
    if (!expect_true(meta->rule_layout_name == "monocle", "layout"))
        return false;
    if (!expect_true(meta->rule_persistent.has_value() && !meta->rule_persistent.value(), "persistent:false"))
        return false;
    if (!expect_true(meta->rule_default.has_value() && meta->rule_default.value(), "default:true"))
        return false;
    if (!expect_true(meta->rule_default_name == "media", "defaultName"))
        return false;
    if (!expect_true(meta->on_created_empty_cmd == "foot", "on-created-empty"))
        return false;
    return true;
}

/* Bracket selector lines merge policy without creating bogus registry names. */
static bool test_bracket_selector_rule_line() {
    workspace_registry_clear();
    ConfVars vars;

    wm::config::parse::workspace::apply_workspace_line("w[t1], gapsout:50", vars, "<unit>", 1U);
    wm::config::parse::workspace::apply_workspace_line("1", vars, "<unit>", 2U);

    if (!expect_true(workspace_registry_count() == 1U, "selector line does not append registry row"))
        return false;
    if (!expect_true(workspace_registry_find_by_name("w[t1]") == nullptr, "selector is not stored as workspace name"))
        return false;
    return true;
}

int main() {
    if (!test_numeric_selector_with_name_and_rule())
        return EXIT_FAILURE;
    if (!test_special_selector_with_rule())
        return EXIT_FAILURE;
    if (!test_name_selector_expands_variable())
        return EXIT_FAILURE;
    if (!test_multiple_non_rule_tokens_throw())
        return EXIT_FAILURE;
    if (!test_empty_name_selector_throws())
        return EXIT_FAILURE;
    if (!test_display_name_with_colon_is_not_misclassified_as_rule())
        return EXIT_FAILURE;
    if (!test_quoted_comma_display_name_is_parsed_as_one_token())
        return EXIT_FAILURE;
    if (!test_unclosed_quote_throws())
        return EXIT_FAILURE;
    if (!test_display_name_findable_by_registry_name())
        return EXIT_FAILURE;
    if (!test_bare_name_appends_workspace())
        return EXIT_FAILURE;
    if (!test_bare_name_redundant_is_noop())
        return EXIT_FAILURE;
    if (!test_numeric_id_only_fills_sparse_defaults())
        return EXIT_FAILURE;
    if (!test_numeric_id_with_name_overrides_default())
        return EXIT_FAILURE;
    if (!test_workspace_rule_keys_merge())
        return EXIT_FAILURE;
    if (!test_bracket_selector_rule_line())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
