/* Unit tests for monitor persist key parse/format (tree-state monitor field). */
#include "monitor/persist_key.hpp"

#include <cassert>
#include <string>

int main() {
    using wm::monitor::format_persist_monitor_key;
    using wm::monitor::parse_persist_monitor_key;
    using wm::monitor::persist_monitor_key_is_numeric;

    assert(persist_monitor_key_is_numeric("0"));
    assert(persist_monitor_key_is_numeric("12"));
    assert(!persist_monitor_key_is_numeric(""));
    assert(!persist_monitor_key_is_numeric("DP-1"));
    assert(!persist_monitor_key_is_numeric("1a"));

    {
        const auto k = parse_persist_monitor_key("0");
        assert(k.has_value());
        assert(k->numeric);
        assert(k->num == 0U);
        assert(k->name.empty());
    }
    {
        const auto k = parse_persist_monitor_key("42");
        assert(k.has_value());
        assert(k->numeric);
        assert(k->num == 42U);
    }
    {
        const auto k = parse_persist_monitor_key("DP-1");
        assert(k.has_value());
        assert(!k->numeric);
        assert(k->name == "DP-1");
    }
    assert(!parse_persist_monitor_key("").has_value());

    assert(format_persist_monitor_key("HDMI-A-1", 3) == "HDMI-A-1");
    assert(format_persist_monitor_key("", 3) == "3");
    assert(format_persist_monitor_key(std::string_view{}, 0) == "0");

    return 0;
}
