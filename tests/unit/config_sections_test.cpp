/* Unit tests for section block parsers (`misc`, `general`, `dwindle`). */
#include "config.hpp"
#include "config/parse/sections.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <sstream>
#include <stdexcept>
#include <string>

/* ---- ConfigState instance for section parser linkage ---- */
ConfigState g_config;

/* Sections parser depends on this symbol through general layout parsing path. */
namespace wm::config::parse::general {
    void (*layout_by_name(std::string_view))(Monitor*) {
        return nullptr;
    }
}

static bool expect_true(bool cond, const char* case_name) {
    if (cond)
        return true;
    fprintf(stderr, "sections case failed: %s\n", case_name);
    return false;
}

static bool expect_eq_u(unsigned int got, unsigned int want, const char* case_name) {
    if (got == want)
        return true;
    fprintf(stderr, "sections case failed: %s (got=%u want=%u)\n", case_name, got, want);
    return false;
}

static bool expect_eq_i(int got, int want, const char* case_name) {
    if (got == want)
        return true;
    fprintf(stderr, "sections case failed: %s (got=%d want=%d)\n", case_name, got, want);
    return false;
}

static bool expect_eq_f(float got, float want, const char* case_name) {
    if (got == want)
        return true;
    fprintf(stderr, "sections case failed: %s (got=%f want=%f)\n", case_name, static_cast<double>(got), static_cast<double>(want));
    return false;
}

static bool expect_eq_s(const std::string& got, const std::string& want, const char* case_name) {
    if (got == want)
        return true;
    fprintf(stderr, "sections case failed: %s (got=[%s] want=[%s])\n", case_name, got.c_str(), want.c_str());
    return false;
}

static bool test_misc_block_parses_known_keys() {
    g_config.conf_vars.clear();
    g_config.conf_vars["FONT"] = "Iosevka";
    g_config.wm_misc           = WmMiscConf{};

    std::istringstream in("font_family = \"$FONT\"\nfocus_on_activate = true\nmouse_move_focuses_monitor = false\nbackground_color = \"#101010\"\n}\n");
    unsigned           lineno = 0;
    wm::config::parse::sections::parse_misc_block(in, lineno, "<unit>");

    return expect_eq_s(g_config.wm_misc.font_family, "Iosevka", "misc font_family expand") &&
        expect_eq_i(g_config.wm_misc.focus_on_activate ? 1 : 0, 1, "misc focus_on_activate") &&
        expect_eq_i(g_config.wm_misc.mouse_move_focuses_monitor ? 1 : 0, 0, "misc mouse_move_focuses_monitor") &&
        expect_eq_s(g_config.wm_misc.background_color, "#101010", "misc background_color");
}

static bool test_misc_block_unknown_key_throws() {
    std::istringstream in("unknown_key = 1\n}\n");
    unsigned           lineno = 0;
    try {
        wm::config::parse::sections::parse_misc_block(in, lineno, "<unit>");
        return expect_true(false, "misc unknown key must throw");
    } catch (const std::runtime_error&) { return true; }
}

static bool test_general_block_parses_snap_and_border() {
    g_config.snap_enabled = 1;
    g_config.snap         = 32U;
    g_config.borderpx     = 1U;

    std::istringstream in("snap {\nenabled = false\nwindow_gap = 12\n}\nborder_size = 3\n}\n");
    unsigned           lineno = 0;
    wm::config::parse::sections::parse_general_block(in, lineno, "<unit>");

    return expect_eq_i(g_config.snap_enabled, 0, "general snap.enabled") && expect_eq_u(g_config.snap, 12U, "general snap.window_gap") &&
        expect_eq_u(g_config.borderpx, 3U, "general border_size");
}

static bool test_dwindle_block_parses_known_keys() {
    g_config.dwindle_preserve_split         = 0;
    g_config.dwindle_force_split            = 0;
    g_config.dwindle_use_active_for_splits  = 1;
    g_config.dwindle_default_split_ratio    = 1.0f;
    g_config.dwindle_split_width_multiplier = 1.0f;

    std::istringstream in("preserve_split = true\nforce_split = 1\nuse_active_for_splits = false\ndefault_split_ratio = 0.7\nsplit_width_multiplier = 1.2\n}\n");
    unsigned           lineno = 0;
    wm::config::parse::sections::parse_dwindle_block(in, lineno, "<unit>");

    return expect_eq_i(g_config.dwindle_preserve_split, 1, "dwindle preserve_split") && expect_eq_i(g_config.dwindle_force_split, 1, "dwindle force_split") &&
        expect_eq_i(g_config.dwindle_use_active_for_splits, 0, "dwindle use_active_for_splits") &&
        expect_eq_f(g_config.dwindle_default_split_ratio, 0.7f, "dwindle default_split_ratio") &&
        expect_eq_f(g_config.dwindle_split_width_multiplier, 1.2f, "dwindle split_width_multiplier");
}

int main() {
    if (!test_misc_block_parses_known_keys())
        return EXIT_FAILURE;
    if (!test_misc_block_unknown_key_throws())
        return EXIT_FAILURE;
    if (!test_general_block_parses_snap_and_border())
        return EXIT_FAILURE;
    if (!test_dwindle_block_parses_known_keys())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
