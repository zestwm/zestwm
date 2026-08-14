/* Unit tests for config parse helpers (`strip_line_comment`, `expand_all`). */
#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <limits>
#include <map>
#include <string>

/* Assert equality for one string-returning parser helper case. */
static bool expect_string(const std::string& got, const std::string& want, const char* case_name) {
    if (got == want)
        return true;
    fprintf(stderr, "config-parse case failed: %s\n", case_name);
    fprintf(stderr, "  expected: [%s]\n", want.c_str());
    fprintf(stderr, "  got:      [%s]\n", got.c_str());
    return false;
}

/* Assert bool parser success with expected value. */
static bool expect_bool_parse(std::string_view input, bool expected, const char* case_name) {
    const auto parsed = wm::config::values::parse_bool_val(input);
    if (!parsed || *parsed != expected) {
        fprintf(stderr, "config-parse case failed: %s\n", case_name);
        return false;
    }
    return true;
}

/* Assert parser returns failure for invalid scalar token. */
template <typename Fn>
static bool expect_parse_fail(Fn&& fn, std::string_view input, const char* case_name) {
    const auto parsed = fn(input);
    if (parsed) {
        fprintf(stderr, "config-parse case failed: %s\n", case_name);
        return false;
    }
    return true;
}

/* Validate comment stripping behavior for quoted strings and true comments. */
static bool test_strip_line_comment() {
    if (!expect_string(wm::config::parse::strip_line_comment("exec = echo \"hello # world\""), "exec = echo \"hello # world\"", "hash preserved inside double quotes"))
        return false;

    if (!expect_string(wm::config::parse::strip_line_comment("exec = echo \"quote: \\\" # still string\""), "exec = echo \"quote: \\\" # still string\"",
                       "escaped quote keeps parser in quoted mode"))
        return false;

    if (!expect_string(wm::config::parse::strip_line_comment("exec = echo \"literal hash \\# not comment\""), "exec = echo \"literal hash \\# not comment\"",
                       "escaped hash preserved inside double quotes"))
        return false;

    if (!expect_string(wm::config::parse::strip_line_comment("exec = run \\# not-a-comment"), "exec = run \\# not-a-comment",
                       "escaped hash outside quotes does not start comment"))
        return false;

    if (!expect_string(wm::config::parse::strip_line_comment("exec = run # trailing comment"), "exec = run", "trailing comment removed"))
        return false;

    if (!expect_string(wm::config::parse::strip_line_comment("col.active_border = #AABBCC"), "col.active_border = #AABBCC", "hex color token is not treated as comment"))
        return false;

    return true;
}

/* Validate recursive expansion convergence and cycle handling. */
static bool test_expand_all() {
    if (!expect_string(wm::config::expand::expand_all("${A}", {{"A", "$B"}, {"B", "ok"}}), "ok", "multi-step expansion converges"))
        return false;

    if (!expect_string(wm::config::expand::expand_all("${A}", {{"A", "$B"}, {"B", "$A"}}), "$A", "two-variable cycle stabilizes without infinite loop"))
        return false;

    if (!expect_string(wm::config::expand::expand_all("$A", {{"A", "$A"}}), "$A", "self-cycle exits cleanly"))
        return false;

    return true;
}

/* Validate scalar value parsers for trimming, base handling, and garbage rejection. */
static bool test_values() {
    if (!expect_bool_parse("  TrUe  ", true, "bool parser trims and ignores ASCII case"))
        return false;
    if (!expect_bool_parse(" off ", false, "bool parser recognizes off=false"))
        return false;
    if (!expect_parse_fail(wm::config::values::parse_bool_val, "truthy", "bool parser rejects unknown token"))
        return false;

    if (!expect_parse_fail([](std::string_view s) { return wm::config::values::parse_int_val(s); }, "42abc", "int parser rejects trailing garbage"))
        return false;
    if (!expect_parse_fail([](std::string_view s) { return wm::config::values::parse_uint_val(s); }, "-1", "uint parser rejects negative literal"))
        return false;

    const auto int_hex = wm::config::values::parse_int_val(" 0x10 ");
    if (!int_hex || *int_hex != 16) {
        fprintf(stderr, "config-parse case failed: int parser keeps base-0 prefix behavior\n");
        return false;
    }

    const auto float_ok = wm::config::values::parse_float_val(" 1.5 ");
    if (!float_ok || *float_ok != 1.5f) {
        fprintf(stderr, "config-parse case failed: float parser trims input\n");
        return false;
    }

    if (!expect_parse_fail(wm::config::values::parse_float_val, "1.0f", "float parser rejects trailing suffix"))
        return false;
    if (!expect_parse_fail(wm::config::values::parse_double_val, "nanxyz", "double parser rejects trailing garbage"))
        return false;

    const std::string int_overflow = std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1LL);
    if (!expect_parse_fail([](std::string_view s) { return wm::config::values::parse_int_val(s); }, int_overflow, "int parser rejects INT_MAX+1"))
        return false;

    const std::string uint_overflow = std::to_string(static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max()) + 1ULL);
    if (!expect_parse_fail([](std::string_view s) { return wm::config::values::parse_uint_val(s); }, uint_overflow, "uint parser rejects UINT_MAX+1"))
        return false;

    if (!expect_parse_fail(wm::config::values::parse_float_val, "1e1000", "float parser rejects overflow literal"))
        return false;
    if (!expect_parse_fail(wm::config::values::parse_double_val, "1e10000", "double parser rejects overflow literal"))
        return false;

    return true;
}

int main() {
    if (!test_strip_line_comment())
        return EXIT_FAILURE;
    if (!test_expand_all())
        return EXIT_FAILURE;
    if (!test_values())
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
