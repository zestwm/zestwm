/* Monitor selector: resolve by Monitor.num or Monitor.output_name (no parallel RandR index map). */
#include "monitor_select.hpp"

#include "state/runtime_authority.hpp"
#include "types.hpp"

#include <cctype>
#include <climits>
#include <cstdlib>
#include <string>

[[nodiscard]] Monitor* monitor_select_resolve(std::string_view selector) {
    if (selector.empty())
        return nullptr;
    bool all_digit = true;
    for (const unsigned char ch : selector) {
        if (!std::isdigit(ch)) {
            all_digit = false;
            break;
        }
    }
    if (all_digit) {
        const std::string selector_copy(selector);
        char*             end    = nullptr;
        const long        parsed = std::strtol(selector_copy.c_str(), &end, 10);
        if (!end || *end != '\0' || parsed < 0 || parsed > INT_MAX)
            return nullptr;
        const int want = static_cast<int>(parsed);
        for (Monitor* mon : wm::state::all_monitors()) {
            if (mon->num == want)
                return mon;
        }
        return nullptr;
    }
    for (Monitor* mon : wm::state::all_monitors()) {
        if (!mon->output_name.empty() && mon->output_name == selector)
            return mon;
    }
    return nullptr;
}

[[nodiscard]] std::string monitor_select_output_name_for_num(int monitor_num) {
    for (Monitor* mon : wm::state::all_monitors()) {
        if (mon->num == monitor_num)
            return mon->output_name;
    }
    return {};
}
