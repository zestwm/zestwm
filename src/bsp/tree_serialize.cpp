/* Pure BSP tree-state format: serialize/parse of SerializedNode trees, floating suffix
 * records, and read-only conversion from a live LayoutNode tree. No X11/registry deps. */
#include "bsp/tree_serialize.hpp"
#include "types.hpp"

#include <cctype>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wm::bsp {
    namespace {
        /* Append an unsigned value as decimal to `out`. */
        void append_uint(std::string& out, unsigned long v) {
            out += std::to_string(v);
        }

        /* Parse a decimal run at `pos`; advances `pos` past the digits. False when not a digit. */
        [[nodiscard]] bool parse_ulong(std::string_view s, std::size_t& pos, unsigned long& out) {
            if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])))
                return false;
            unsigned long v = 0UL;
            std::size_t   i = pos;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                v = v * 10UL + static_cast<unsigned long>(s[i] - '0');
                ++i;
            }
            pos = i;
            out = v;
            return true;
        }

        /* True when `s[pos]` is the expected separator char; consumes it on match. */
        [[nodiscard]] bool expect_char(std::string_view s, std::size_t& pos, char ch) {
            if (pos >= s.size() || s[pos] != ch)
                return false;
            ++pos;
            return true;
        }

        /* Wire ratio bounds, matching the historical serializer clamp. */
        constexpr int kRatioMin = 500;
        constexpr int kRatioMax = 9500;
    } // namespace

    std::string serialize_tree(const SerializedNode& root) {
        std::string out;
        /* Recursive lambda; depth follows BSP nesting (small in practice). */
        const std::function<void(const SerializedNode&)> walk = [&](const SerializedNode& n) {
            if (n.is_split) {
                out += "S(";
                append_uint(out, static_cast<unsigned long>(n.axis));
                out += ":";
                append_uint(out, static_cast<unsigned long>(n.ratio_10000));
                out += ":";
                if (n.first)
                    walk(*n.first);
                out += ":";
                if (n.second)
                    walk(*n.second);
                out += ")";
                return;
            }
            out += "G(";
            append_uint(out, static_cast<unsigned long>(n.grouped.groupmode ? 1 : 0));
            out += ":";
            append_uint(out, static_cast<unsigned long>(n.grouped.activewin));
            out += ":";
            for (std::size_t i = 0U; i < n.grouped.wins.size(); ++i) {
                if (i)
                    out += ",";
                append_uint(out, static_cast<unsigned long>(n.grouped.wins[i]));
            }
            out += ")";
        };
        walk(root);
        return out;
    }

    [[nodiscard]] std::optional<SerializedNode> parse_tree(std::string_view s, std::size_t& pos) {
        if (pos >= s.size())
            return std::nullopt;

        if (s[pos] == 'S') {
            if (pos + 1U >= s.size() || s[pos + 1U] != '(')
                return std::nullopt;
            pos += 2U;
            unsigned long axis = 0UL;
            if (!parse_ulong(s, pos, axis) || !expect_char(s, pos, ':'))
                return std::nullopt;
            unsigned long ratio = 0UL;
            if (!parse_ulong(s, pos, ratio) || !expect_char(s, pos, ':'))
                return std::nullopt;
            auto first = parse_tree(s, pos);
            if (!first || !expect_char(s, pos, ':'))
                return std::nullopt;
            auto second = parse_tree(s, pos);
            if (!second || !expect_char(s, pos, ')'))
                return std::nullopt;
            SerializedNode n;
            n.is_split    = true;
            n.axis        = axis ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
            n.ratio_10000 = static_cast<int>(ratio);
            n.first       = std::make_unique<SerializedNode>(std::move(*first));
            n.second      = std::make_unique<SerializedNode>(std::move(*second));
            return n;
        }

        if (s[pos] != 'G' || pos + 1U >= s.size() || s[pos + 1U] != '(')
            return std::nullopt;
        pos += 2U;
        unsigned long groupmode = 0UL;
        if (!parse_ulong(s, pos, groupmode) || !expect_char(s, pos, ':'))
            return std::nullopt;
        unsigned long activewin = 0UL;
        if (!parse_ulong(s, pos, activewin) || !expect_char(s, pos, ':'))
            return std::nullopt;
        SerializedNode n;
        n.grouped.groupmode = groupmode ? 1 : 0;
        n.grouped.activewin = static_cast<Window>(activewin);
        while (pos < s.size() && s[pos] != ')') {
            unsigned long win = 0UL;
            if (!parse_ulong(s, pos, win))
                break;
            n.grouped.wins.push_back(static_cast<Window>(win));
            if (pos < s.size() && s[pos] == ',')
                ++pos;
        }
        if (!expect_char(s, pos, ')'))
            return std::nullopt;
        return n;
    }

    [[nodiscard]] std::optional<SerializedNode> serialized_from_layout(const LayoutNode* root) {
        if (!root)
            return std::nullopt;
        SerializedNode n;
        if (root->type == NODE_SPLIT) {
            n.is_split = true;
            n.axis     = root->split.axis;
            int ratio  = static_cast<int>(root->split.ratio * 10000.0f);
            if (ratio < kRatioMin)
                ratio = kRatioMin;
            if (ratio > kRatioMax)
                ratio = kRatioMax;
            n.ratio_10000 = ratio;
            if (root->split.first) {
                if (auto f = serialized_from_layout(root->split.first.get()))
                    n.first = std::make_unique<SerializedNode>(std::move(*f));
            }
            if (root->split.second) {
                if (auto s2 = serialized_from_layout(root->split.second.get()))
                    n.second = std::make_unique<SerializedNode>(std::move(*s2));
            }
            return n;
        }
        n.grouped.groupmode = root->grouped.groupmode ? 1 : 0;
        if (root->grouped.active < root->grouped.clients.size() && root->grouped.clients[root->grouped.active])
            n.grouped.activewin = root->grouped.clients[root->grouped.active]->win;
        for (const Client* c : root->grouped.clients) {
            if (c)
                n.grouped.wins.push_back(c->win);
        }
        return n;
    }

    std::vector<FloatingRecord> parse_floating_suffix(std::string_view suffix) {
        std::vector<FloatingRecord> out;
        /* suffix begins at `|F(`; locate the inner payload between '(' and ')'. */
        const std::size_t open  = suffix.find('(');
        const std::size_t close = suffix.find(')', open == std::string_view::npos ? 0U : open);
        if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
            return out;
        const std::string_view body = suffix.substr(open + 1U, close - open - 1U);
        std::size_t            p    = 0U;
        while (p < body.size()) {
            unsigned long vals[5] = {0UL, 0UL, 0UL, 0UL, 0UL};
            int           vi      = 0;
            while (p < body.size() && vi < 5) {
                if (!std::isdigit(static_cast<unsigned char>(body[p]))) {
                    ++p;
                    continue;
                }
                unsigned long v = 0UL;
                while (p < body.size() && std::isdigit(static_cast<unsigned char>(body[p]))) {
                    v = v * 10UL + static_cast<unsigned long>(body[p] - '0');
                    ++p;
                }
                vals[vi++] = v;
                if (p < body.size() && body[p] == ':')
                    ++p;
            }
            /* skip trailing comma / stray chars until next digit or end */
            while (p < body.size() && !std::isdigit(static_cast<unsigned char>(body[p])))
                ++p;
            if (vi < 5)
                continue;
            FloatingRecord r;
            r.win = static_cast<Window>(vals[0]);
            r.x   = static_cast<int>(vals[1]);
            r.y   = static_cast<int>(vals[2]);
            r.w   = static_cast<int>(vals[3]);
            r.h   = static_cast<int>(vals[4]);
            out.push_back(r);
        }
        return out;
    }

    std::string format_floating_suffix(std::span<const FloatingRecord> records) {
        std::string out;
        if (records.empty())
            return out;
        out = "|F(";
        for (std::size_t i = 0U; i < records.size(); ++i) {
            if (i)
                out += ",";
            const FloatingRecord& r = records[i];
            out += std::to_string(static_cast<unsigned long>(r.win));
            out += ":";
            out += std::to_string(r.x);
            out += ":";
            out += std::to_string(r.y);
            out += ":";
            out += std::to_string(r.w);
            out += ":";
            out += std::to_string(r.h);
        }
        out += ")";
        return out;
    }
} // namespace wm::bsp
