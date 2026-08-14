/* Unit tests for zestctl whitespace tokenizer behavior (`split_ws`). */
#include "zestctl/helpers.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <initializer_list>
#include <string>
#include <vector>

/* Compare actual tokens with expected values for one test case. */
static bool expect_tokens(
  const std::string& input,
  std::initializer_list<const char*> expected,
  const char* case_name
) {
    const std::vector<std::string> got = split_ws(input);
    const std::vector<std::string> want(expected.begin(), expected.end());
    if (got == want)
        return true;
    fprintf(stderr, "split_ws case failed: %s\n", case_name);
    fprintf(stderr, "  input: [%s]\n", input.c_str());
    fprintf(stderr, "  expected token count: %zu, got: %zu\n", want.size(), got.size());
    for (size_t i = 0; i < want.size(); ++i) {
        fprintf(stderr, "  expected[%zu]=[%s]\n", i, want[i].c_str());
    }
    for (size_t i = 0; i < got.size(); ++i) {
        fprintf(stderr, "  got[%zu]=[%s]\n", i, got[i].c_str());
    }
    return false;
}

/* Run split_ws unit coverage for mixed and boundary whitespace inputs. */
int main() {
    if (!expect_tokens("", {}, "empty input"))
        return EXIT_FAILURE;
    if (!expect_tokens("    \t\t \n", {}, "whitespace only"))
        return EXIT_FAILURE;
    if (!expect_tokens("dispatch workspace 4", {"dispatch", "workspace", "4"}, "single-space separators"))
        return EXIT_FAILURE;
    if (!expect_tokens(
          "  dispatch\tworkspace\nspecial:scratchpad\t",
          {"dispatch", "workspace", "special:scratchpad"},
          "leading/trailing and mixed whitespace"
        ))
        return EXIT_FAILURE;
    if (!expect_tokens("one\n\ntwo\t\tthree", {"one", "two", "three"}, "repeated separators"))
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
