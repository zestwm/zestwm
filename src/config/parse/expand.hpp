/* Config parser expansion helpers shared across config modules. */
#pragma once

#include "config.hpp"

#include <string>
#include <string_view>

namespace wm::config::expand {

    /* Validate identifier syntax used by config variable assignment and ${...} tokens. */
    [[nodiscard]] bool valid_conf_var_name(std::string_view name) noexcept;

    /* Repeat single-pass expansion until fixed-point, cycle detection, or bounded pass limit.
     *
     * Contract:
     * - Never loops indefinitely on cyclic variable references.
     * - Returns the last non-repeating intermediate expansion state.
     */
    [[nodiscard]] std::string expand_all(std::string_view in, const ConfVars& conf_vars);

} /* namespace wm::config::expand */
