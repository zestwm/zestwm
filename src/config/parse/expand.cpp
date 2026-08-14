/* Config parser expansion helpers implementation. */
#include "config/parse/expand.hpp"

#include "config/parse/utils.hpp"

#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace wm::config::expand {
    namespace {
        /* Max expansion passes:
         * - 64 is far above typical config expansion depth (< 5 in practice).
         * - Prevents infinite recursion from circular references.
         * - Expansion runs on config parse cold-path, so this bound is cheap.
         */
        constexpr std::size_t        kMaxExpansionPasses = 64;

        [[nodiscard]] constexpr bool is_ascii_alpha(unsigned char ch) noexcept {
            return (ch >= static_cast<unsigned char>('A') && ch <= static_cast<unsigned char>('Z')) ||
                (ch >= static_cast<unsigned char>('a') && ch <= static_cast<unsigned char>('z'));
        }

        [[nodiscard]] constexpr bool is_ascii_digit(unsigned char ch) noexcept {
            return ch >= static_cast<unsigned char>('0') && ch <= static_cast<unsigned char>('9');
        }

        [[nodiscard]] constexpr bool is_valid_var_char(char ch, bool first) noexcept {
            const auto uch = static_cast<unsigned char>(ch);
            return first ? (is_ascii_alpha(uch) || ch == '_') : (is_ascii_alpha(uch) || is_ascii_digit(uch) || ch == '_');
        }

        [[nodiscard]] static bool is_var_identifier_char(char ch) noexcept {
            const unsigned char uch = static_cast<unsigned char>(ch);
            return std::isalnum(uch) || ch == '_';
        }
    } // namespace

    /* Validate identifier syntax for config variable names and ${...} expansion. */
    [[nodiscard]] bool valid_conf_var_name(std::string_view name) noexcept {
        if (name.empty())
            return false;

        for (size_t i = 0; i < name.size(); ++i)
            if (!is_valid_var_char(name[i], i == 0))
                return false;
        return true;
    }

    /* Resolve expansion key from config-defined vars first, then process environment. */
    [[nodiscard]] std::string lookup_var(std::string_view name, const ConfVars& conf_vars) {
        const auto it = conf_vars.find(name);
        if (it != conf_vars.end())
            return it->second;

        /* Keep one owned key for getenv fallback C API. */
        const std::string key(name);
        const char*       env = std::getenv(key.c_str());
        return env ? std::string(env) : std::string();
    }

    /* Expand leading tilde forms (~ and ~/...) using HOME when available. */
    [[nodiscard]] std::string tilde_expand(std::string_view input) {
        if (input.empty() || input[0] != '~' || (input.size() > 1 && input[1] != '/'))
            return std::string(input);

        const char* home = std::getenv("HOME");
        if (!home)
            return std::string(input);

        std::string expanded(home);
        expanded.append(input.substr(1));
        return expanded;
    }

    /* Apply one non-recursive expansion pass over $VAR and ${VAR} tokens. */
    [[nodiscard]] std::string expand_one_pass(std::string_view in, const ConfVars& conf_vars) {
        std::string s = tilde_expand(in);
        std::string o;
        o.reserve(s.size());

        for (size_t pos = 0; pos < s.size();) {
            if (s[pos] != '$') {
                o += s[pos++];
                continue;
            }
            if (pos + 1 < s.size() && s[pos + 1] == '{') {
                const size_t end = s.find('}', pos + 2);
                if (end == std::string::npos) {
                    o += '$';
                    pos++;
                    continue;
                }
                const std::string_view var_content{s.data() + pos + 2, end - pos - 2};
                const std::string      name = wm::config::parse::trim(std::string{var_content});
                if (!valid_conf_var_name(name)) {
                    o.append(s, pos, end - pos + 1);
                    pos = end + 1;
                    continue;
                }
                o += lookup_var(name, conf_vars);
                pos = end + 1;
                continue;
            }

            size_t i = pos + 1;
            while (i < s.size() && is_var_identifier_char(s[i]))
                ++i;
            if (i == pos + 1) {
                o += '$';
                pos++;
                continue;
            }

            o += lookup_var(std::string_view{s.data() + pos + 1, i - pos - 1}, conf_vars);
            pos = i;
        }

        return o;
    }

    /* Repeat one-pass expansion until stable output or bounded pass cap. */
    [[nodiscard]] std::string expand_all(std::string_view in, const ConfVars& conf_vars) {
        std::string                     s(in);
        std::unordered_set<std::string> seen;
        seen.reserve(kMaxExpansionPasses);
        for (std::size_t pass = 0; pass < kMaxExpansionPasses; ++pass) {
            seen.insert(s);
            std::string n = expand_one_pass(s, conf_vars);
            if (n == s)
                break;
            if (seen.contains(n))
                break;
            s = std::move(n);
        }
        return s;
    }

} /* namespace wm::config::expand */
