/* Config parser value conversion helpers shared across config modules.
 *
 * Role:
 * - Convert scalar config tokens into typed values with strict full-token checks.
 *
 * Contract model:
 * - Success: typed value in `std::expected`.
 * - Failure: stable human-readable message suitable for parse diagnostics.
 *
 * Notes:
 * - Helpers trim ASCII surrounding whitespace before conversion.
 * - Helpers reject trailing garbage (e.g. `12abc`).
 */
#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace wm::config::values {

    /* Parse common boolean spellings used in zestwm.conf values.
     *
     * Accepted true tokens:  1, true, yes, on
     * Accepted false tokens: 0, false, no, off
     * Matching is ASCII case-insensitive.
     */
    [[nodiscard]] std::expected<bool, std::string> parse_bool_val(std::string_view value);
    /* Parse signed integer using strtol-compatible base handling (default base 0).
     *
     * - Base=0 keeps C-style prefix behavior (0x..., 0...).
     * - Value must fit into target `int`.
     */
    [[nodiscard]] std::expected<int, std::string> parse_int_val(std::string_view value, int base = 0);
    /* Parse unsigned integer using strtoul-compatible base handling (default base 0).
     *
     * - Base=0 keeps C-style prefix behavior (0x..., 0...).
     * - Value must fit into target `unsigned int`.
     */
    [[nodiscard]] std::expected<unsigned int, std::string> parse_uint_val(std::string_view value, int base = 0);
    /* Parse floating-point scalar into float with strict token validation.
     *
     * - Rejects overflow/underflow and trailing garbage.
     * - Requires representable range for `float`.
     */
    [[nodiscard]] std::expected<float, std::string> parse_float_val(std::string_view value);
    /* Parse floating-point scalar into double with strict token validation.
     *
     * - Rejects overflow/underflow and trailing garbage.
     */
    [[nodiscard]] std::expected<double, std::string> parse_double_val(std::string_view value);

} /* namespace wm::config::values */
