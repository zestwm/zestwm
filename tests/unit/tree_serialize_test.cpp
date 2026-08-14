/*
 * Round-trip self-check for the pure BSP tree-state format (serialize -> parse -> serialize).
 * No WM session, no fixtures: builds SerializedNode trees by hand and asserts format symmetry.
 */
#include "bsp/tree_serialize.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using wm::bsp::FloatingRecord;
    using wm::bsp::SerializedNode;
    using wm::bsp::format_floating_suffix;
    using wm::bsp::parse_floating_suffix;
    using wm::bsp::parse_tree;
    using wm::bsp::serialize_tree;

    /* Build a grouped leaf node. */
    [[nodiscard]] SerializedNode grouped(int groupmode, Window activewin, std::vector<Window> wins) {
        SerializedNode n;
        n.grouped.groupmode = groupmode;
        n.grouped.activewin = activewin;
        n.grouped.wins      = std::move(wins);
        return n;
    }

    /* Build a split node taking ownership of two children. */
    [[nodiscard]] SerializedNode split(SplitAxis axis, int ratio_10000, SerializedNode first, SerializedNode second) {
        SerializedNode n;
        n.is_split    = true;
        n.axis        = axis;
        n.ratio_10000 = ratio_10000;
        n.first       = std::make_unique<SerializedNode>(std::move(first));
        n.second      = std::make_unique<SerializedNode>(std::move(second));
        return n;
    }

    /* Assert serialize -> parse -> serialize is identity for a given tree. */
    void check_roundtrip(const SerializedNode& root) {
        const std::string s1  = serialize_tree(root);
        std::size_t       pos = 0U;
        auto              sn  = parse_tree(s1, pos);
        assert(sn.has_value());
        assert(pos == s1.size());
        const std::string s2 = serialize_tree(*sn);
        assert(s1 == s2);
    }

    void test_single_grouped() {
        check_roundtrip(grouped(0, 100U, {100U, 200U, 300U}));
    }

    void test_groupmode_active() {
        check_roundtrip(grouped(1, 200U, {100U, 200U}));
    }

    void test_empty_grouped() {
        /* No members, no active: still round-trips. */
        check_roundtrip(grouped(0, 0U, {}));
    }

    void test_simple_split() {
        check_roundtrip(split(SPLIT_HORIZONTAL, 5000, grouped(0, 10U, {10U}), grouped(0, 20U, {20U})));
    }

    void test_vertical_split_clamped_ratio() {
        check_roundtrip(split(SPLIT_VERTICAL, 9500, grouped(0, 11U, {11U, 12U}), grouped(1, 21U, {21U})));
    }

    void test_nested_split() {
        check_roundtrip(split(SPLIT_HORIZONTAL, 3000, grouped(0, 1U, {1U, 2U}), split(SPLIT_VERTICAL, 7000, grouped(0, 3U, {3U}), grouped(0, 4U, {4U}))));
    }

    void test_parse_wire_exact() {
        /* A known on-wire string must parse and re-serialize to the same bytes. */
        const std::string wire = "S(1:7000:G(0:3:3):G(1:4:4))";
        std::size_t       pos  = 0U;
        auto              sn   = parse_tree(wire, pos);
        assert(sn.has_value());
        assert(pos == wire.size());
        assert(serialize_tree(*sn) == wire);
    }

    void test_parse_rejects_malformed() {
        const std::string_view bad[] = {
            "", "X(0)", "S(0:5000:)", "G(0:1:", "S(0:5000:G(0:1:1))", "G(0:1:1,2",
        };
        for (const std::string_view s : bad) {
            std::size_t pos = 0U;
            const auto  sn  = parse_tree(s, pos);
            assert(!sn.has_value());
        }
    }

    void test_floating_suffix_roundtrip() {
        std::vector<FloatingRecord> recs = {
            {100U, 10, 20, 300, 400},
            {200U, 50, 60, 700, 800},
        };
        const std::string s = format_floating_suffix(recs);
        assert(s == "|F(100:10:20:300:400,200:50:60:700:800)");
        const std::vector<FloatingRecord> parsed = parse_floating_suffix(s);
        assert(parsed.size() == 2U);
        assert(parsed[0].win == 100U && parsed[0].x == 10 && parsed[0].y == 20 && parsed[0].w == 300 && parsed[0].h == 400);
        assert(parsed[1].win == 200U && parsed[1].x == 50 && parsed[1].y == 60 && parsed[1].w == 700 && parsed[1].h == 800);
        assert(format_floating_suffix({}) == "");
    }

    void test_floating_suffix_picks_inner_parens() {
        /* Leading garbage before `|F(` must not derail inner-paren location. */
        const std::vector<FloatingRecord> parsed = parse_floating_suffix("junk|F(5:1:2:3:4)");
        assert(parsed.size() == 1U);
        assert(parsed[0].win == 5U && parsed[0].w == 3 && parsed[0].h == 4);
    }
} // namespace

int main() {
    test_single_grouped();
    test_groupmode_active();
    test_empty_grouped();
    test_simple_split();
    test_vertical_split_clamped_ratio();
    test_nested_split();
    test_parse_wire_exact();
    test_parse_rejects_malformed();
    test_floating_suffix_roundtrip();
    test_floating_suffix_picks_inner_parens();
    return 0;
}
