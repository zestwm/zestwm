/* Unit tests for group.focus_removed_window policy tokens. */
#include "config/group_focus_policy.hpp"

#include <cassert>

int main() {
    using wm::config::GroupFocusRemovedPolicy;
    using wm::config::parse_group_focus_removed_policy;

    {
        const auto p = parse_group_focus_removed_policy("history");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::History);
    }
    {
        const auto p = parse_group_focus_removed_policy(" previous ");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::Previous);
    }
    {
        const auto p = parse_group_focus_removed_policy("prev");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::Previous);
    }
    {
        const auto p = parse_group_focus_removed_policy("\"next\"");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::Next);
    }
    {
        const auto p = parse_group_focus_removed_policy("first");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::First);
    }
    {
        const auto p = parse_group_focus_removed_policy("last");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::Last);
    }
    {
        const auto p = parse_group_focus_removed_policy("leave");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::Leave);
    }
    {
        const auto p = parse_group_focus_removed_policy("none");
        assert(p.has_value());
        assert(*p == GroupFocusRemovedPolicy::Leave);
    }
    assert(!parse_group_focus_removed_policy("nope").has_value());
    return 0;
}
