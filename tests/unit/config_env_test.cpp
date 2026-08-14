/* Unit tests for env/envd config directive parser helpers. */
#include "config/env.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

static bool expect_true(bool cond, const char* case_name) {
    if (cond)
        return true;
    fprintf(stderr, "env parser case failed: %s\n", case_name);
    return false;
}

static bool test_apply_env_line_sets_variable() {
    ConfVars vars;
    vars["FOO_VAL"] = "bar";

    ::unsetenv("ZEST_ENV_TEST_KEY");
    wm::config::env::apply_env_line("ZEST_ENV_TEST_KEY, $FOO_VAL", false, "<unit>", 1U, vars);

    const char* got = ::getenv("ZEST_ENV_TEST_KEY");
    const bool  ok  = got && std::string(got) == "bar";
    ::unsetenv("ZEST_ENV_TEST_KEY");
    return expect_true(ok, "setenv with variable expansion");
}

static bool test_apply_env_line_allows_commas_in_value_tail() {
    ConfVars vars;

    ::unsetenv("ZEST_ENV_TEST_CSV");
    wm::config::env::apply_env_line("ZEST_ENV_TEST_CSV, a,b,c", false, "<unit>", 2U, vars);

    const char* got = ::getenv("ZEST_ENV_TEST_CSV");
    const bool  ok  = got && std::string(got) == "a,b,c";
    ::unsetenv("ZEST_ENV_TEST_CSV");
    return expect_true(ok, "value keeps commas after first separator");
}

static bool test_apply_env_line_rejects_invalid_key() {
    ConfVars vars;

    try {
        wm::config::env::apply_env_line("BAD=KEY, value", false, "<unit>", 3U, vars);
        return expect_true(false, "invalid key containing '=' must throw");
    } catch (const std::runtime_error&) { return true; }
}

static bool test_apply_env_line_rejects_non_posix_key_chars() {
    ConfVars vars;

    try {
        wm::config::env::apply_env_line("MY-KEY, value", false, "<unit>", 4U, vars);
        return expect_true(false, "non-POSIX key chars must throw");
    } catch (const std::runtime_error&) { return true; }
}

int main() {
    if (!test_apply_env_line_sets_variable())
        return EXIT_FAILURE;
    if (!test_apply_env_line_allows_commas_in_value_tail())
        return EXIT_FAILURE;
    if (!test_apply_env_line_rejects_invalid_key())
        return EXIT_FAILURE;
    if (!test_apply_env_line_rejects_non_posix_key_chars())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
