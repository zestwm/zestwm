/* Config parser value conversion helpers implementation. */
#include "config/parse/values.hpp"

#include "config/parse/utils.hpp"

#include <cerrno>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cstdlib>

namespace wm::config::values {
    namespace {
        [[nodiscard]] static bool ascii_ieq(std::string_view a, std::string_view b) noexcept {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }
    } // namespace

    /* Build uniform parse error message for typed scalar config values. */
    [[nodiscard]] static std::string make_parse_error(std::string_view kind, std::string_view value) {
        std::string err = "expected ";
        err.append(kind);
        err += ", got '";
        err.append(value);
        err += "'";
        return err;
    }

    /* Parse signed long with strict full-token validation and errno checks.
     *
     * Input is pre-trimmed by caller.
     */
    [[nodiscard]] static std::expected<long, std::string> parse_long_core(std::string_view trimmed_value, int base) {
        std::string input(trimmed_value);
        char*       end = nullptr;

        errno             = 0;
        const long parsed = std::strtol(input.c_str(), &end, base);
        if (end == input.c_str() || *end != '\0' || errno == ERANGE)
            return std::unexpected(make_parse_error("signed integer", trimmed_value));

        return parsed;
    }

    /* Parse unsigned long with strict full-token validation and errno checks.
     *
     * Input is pre-trimmed by caller.
     */
    [[nodiscard]] static std::expected<unsigned long, std::string> parse_ulong_core(std::string_view trimmed_value, int base) {
        std::string input(trimmed_value);
        char*       end = nullptr;

        errno                      = 0;
        const unsigned long parsed = std::strtoul(input.c_str(), &end, base);
        if (end == input.c_str() || *end != '\0' || errno == ERANGE)
            return std::unexpected(make_parse_error("unsigned integer", trimmed_value));

        return parsed;
    }

    /* Parse double with strict full-token validation and errno checks.
     *
     * Input is pre-trimmed by caller.
     */
    [[nodiscard]] static std::expected<double, std::string> parse_double_core(std::string_view trimmed_value) {
        std::string input(trimmed_value);
        char*       end = nullptr;

        errno               = 0;
        const double parsed = std::strtod(input.c_str(), &end);
        if (end == input.c_str() || *end != '\0' || errno == ERANGE)
            return std::unexpected(make_parse_error("floating-point", trimmed_value));

        return parsed;
    }

    /* Parse common boolean literals used in config values. */
    [[nodiscard]] std::expected<bool, std::string> parse_bool_val(std::string_view value) {
        const std::string trimmed = wm::config::parse::trim(value);
        if (ascii_ieq(trimmed, "1") || ascii_ieq(trimmed, "true") || ascii_ieq(trimmed, "yes") || ascii_ieq(trimmed, "on"))
            return true;
        if (ascii_ieq(trimmed, "0") || ascii_ieq(trimmed, "false") || ascii_ieq(trimmed, "no") || ascii_ieq(trimmed, "off"))
            return false;
        return std::unexpected(make_parse_error("boolean (true/false/1/0)", trimmed));
    }

    /* Parse signed integer and validate target int range. */
    [[nodiscard]] std::expected<int, std::string> parse_int_val(std::string_view value, int base) {
        const std::string trimmed = wm::config::parse::trim(value);
        const auto        parsed  = parse_long_core(trimmed, base);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (*parsed < INT_MIN || *parsed > INT_MAX)
            return std::unexpected(make_parse_error("signed integer", trimmed));

        return static_cast<int>(*parsed);
    }

    /* Parse unsigned integer and validate target unsigned int range. */
    [[nodiscard]] std::expected<unsigned int, std::string> parse_uint_val(std::string_view value, int base) {
        const std::string trimmed = wm::config::parse::trim(value);
        const auto        parsed  = parse_ulong_core(trimmed, base);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (*parsed > UINT_MAX)
            return std::unexpected(make_parse_error("unsigned integer", trimmed));

        return static_cast<unsigned int>(*parsed);
    }

    /* Parse floating-point value and validate representable float bounds. */
    [[nodiscard]] std::expected<float, std::string> parse_float_val(std::string_view value) {
        const std::string trimmed = wm::config::parse::trim(value);
        const auto        parsed  = parse_double_core(trimmed);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (*parsed < -FLT_MAX || *parsed > FLT_MAX)
            return std::unexpected(make_parse_error("floating-point", trimmed));

        return static_cast<float>(*parsed);
    }

    /* Parse floating-point value into double with strict token checks. */
    [[nodiscard]] std::expected<double, std::string> parse_double_val(std::string_view value) {
        return parse_double_core(wm::config::parse::trim(value));
    }

} /* namespace wm::config::values */
