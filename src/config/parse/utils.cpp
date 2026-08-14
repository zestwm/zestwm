/* Config parser string helpers implementation. */
#include "config/parse/utils.hpp"

#include <cctype>

namespace wm::config::parse {
    namespace {
        /* Canonical ASCII whitespace set used by trim and related checks. */
        constexpr std::string_view kAsciiWhitespace = " \t\n\r\f\v";

        /* Return true when character belongs to ASCII space class for parser token boundaries. */
        [[nodiscard]] static bool is_ascii_space(char ch) noexcept {
            return std::isspace(static_cast<unsigned char>(ch)) != 0;
        }

        /* Detect hex color literals that start with '#'.
         *
         * Accepted lengths after '#': 3, 4, 6, 8.
         * Token must end at line end or whitespace boundary.
         */
        [[nodiscard]] static bool is_hex_color_literal(std::string_view line, size_t hash_pos) noexcept {
            size_t j = hash_pos + 1;
            while (j < line.size() && std::isxdigit(static_cast<unsigned char>(line[j])))
                ++j;
            const size_t nhex      = j - (hash_pos + 1);
            const bool   valid_len = (nhex == 3U || nhex == 4U || nhex == 6U || nhex == 8U);
            const bool   token_end = (j == line.size()) || is_ascii_space(line[j]);
            return valid_len && token_end;
        }
    } // namespace

    /* Trim leading/trailing ASCII whitespace from raw config token text.
     *
     * Complexity:
     * - O(N) with two bounded scans (`find_first_not_of`, `find_last_not_of`).
     */
    [[nodiscard]] std::string trim(std::string_view s) {
        const size_t start = s.find_first_not_of(kAsciiWhitespace);
        if (start == std::string_view::npos)
            return "";
        const size_t end = s.find_last_not_of(kAsciiWhitespace);
        return std::string(s.substr(start, end - start + 1));
    }

    /* Remove one surrounding quote pair and trim the resulting token.
     *
     * Notes:
     * - Single quotes are not stripped here (parser keeps that distinction).
     * - Exactly one outer pair is removed; nested pairs remain.
     */
    [[nodiscard]] std::string strip_outer_quotes(std::string s) {
        s = trim(s);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return trim(s.substr(1, s.size() - 2));
        return s;
    }

    /* Strip line comments while preserving quoted text and hex color literals.
     *
     * Parsing model:
     * - Tracks single/double quote state per line.
     * - Supports escaped chars inside quoted regions (`\"`, `\\`, etc.).
     * - Treats '#' as comment start only at BOL or after whitespace.
     *
     * Return value:
     * - Always trimmed before return to keep downstream parsing uniform.
     */
    [[nodiscard]] std::string strip_line_comment(std::string line) {
        bool in_single = false;
        bool in_double = false;
        bool escaped   = false;

        for (size_t i = 0; i < line.size(); ++i) {
            const char ch = line[i];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (ch == '\\' && (in_single || in_double)) {
                escaped = true;
                continue;
            }
            if (ch == '\'' && !in_double) {
                in_single = !in_single;
                continue;
            }
            if (ch == '"' && !in_single) {
                in_double = !in_double;
                continue;
            }
            if (ch != '#' || in_single || in_double)
                continue;

            const bool at_bol     = (i == 0);
            const bool prev_space = !at_bol && is_ascii_space(line[i - 1]);
            if (!at_bol && !prev_space)
                continue;

            if (is_hex_color_literal(line, i))
                continue;

            line.resize(i);
            break;
        }
        return trim(line);
    }

} /* namespace wm::config::parse */
