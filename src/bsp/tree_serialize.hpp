/* BSP tree-state persistence format: pure parse/serialize model plus WM-bound glue.
 *
 * Role:
 * - Own the on-wire format of `_NET_ZEST_TREE_STATE` node payloads:
 *   `S(axis:ratio:first:second)` splits and `G(groupmode:activewin:wins...)` grouped
 *   leaves, plus the `|F(win:x:y:w:h,...)` floating-client suffix.
 * - Keep parse and serialize symmetric and free of live WM state so the format can be
 *   round-trip tested without a session.
 *
 * Layering:
 * - `tree_serialize.cpp` holds the pure format (this header's SerializedNode,
 *   serialize_tree, parse_tree, floating suffix records) plus `serialized_from_layout`,
 *   which only *reads* a live `LayoutNode` tree. It links with no X11/registry code.
 * - `tree_serialize_bind.cpp` holds `build_layout_tree` and the workspace-key
 *   encode/decode helpers; those allocate `LayoutNode`s and consult the special
 *   workspace registry, so they link with the WM only.
 *
 * Invariants:
 * - `serialize_tree(parse_tree(s))` reproduces `s` for any well-formed payload that
 *   carries no null split children (the legacy parser cannot represent those either).
 * - `ratio_10000` is the raw wire value (per-10000); `serialized_from_layout` clamps it
 *   to 500..9500 to match the historical serializer.
 */
#pragma once

#include "layout_tree.hpp"
#include "workspace_ref.hpp"
#include "x11/backend.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wm::bsp {

    /* Window-id-only mirror of a grouped leaf: ordered members + active member id. */
    struct SerializedGrouped {
        int                 groupmode = 0; /* 0/1 tab groupmode flag */
        Window              activewin = 0; /* 0 when no active member */
        std::vector<Window> wins;          /* ordered member window ids */
    };

    /* Pure structural representation of one serialized BSP node.
     * Split nodes carry axis + ratio (per-10000) + two children; grouped nodes carry
     * a `SerializedGrouped` payload. Null `first`/`second` mirror a null live child
     * (serializes to empty, not re-parseable, matching legacy behavior). */
    struct SerializedNode {
        bool                            is_split    = false;
        SplitAxis                       axis        = SPLIT_HORIZONTAL;
        int                             ratio_10000 = 5000;
        std::unique_ptr<SerializedNode> first;
        std::unique_ptr<SerializedNode> second;
        SerializedGrouped               grouped;
    };

    /* Serialize a tree to the on-wire `S(...)`/`G(...)` format. Empty root -> "". */
    [[nodiscard]] std::string serialize_tree(const SerializedNode& root);

    /* Parse one tree starting at `pos`; advances `pos` past the consumed node.
     * Returns nullopt on malformed input; `pos` is left at the failing offset. */
    [[nodiscard]] std::optional<SerializedNode> parse_tree(std::string_view s, std::size_t& pos);

    /* Build a SerializedNode tree from a live LayoutNode tree (reads client window ids).
     * Returns nullopt when `root` is null. */
    [[nodiscard]] std::optional<SerializedNode> serialized_from_layout(const LayoutNode* root);

    /* One floating-client record persisted in the `|F(win:x:y:w:h,...)` suffix. */
    struct FloatingRecord {
        Window win = 0;
        int    x = 0, y = 0, w = 0, h = 0;
    };

    /* Parse a `|F(...)` suffix (the leading `|F` and outer parens are located here). */
    [[nodiscard]] std::vector<FloatingRecord> parse_floating_suffix(std::string_view suffix);

    /* Format records as `|F(win:x:y:w:h,...)`; empty span -> "". */
    [[nodiscard]] std::string format_floating_suffix(std::span<const FloatingRecord> records);

    /* ---- WM-bound helpers (link with the WM; allocate LayoutNode / consult registry) ---- */

    /* Resolve live clients into a freshly allocated LayoutNode tree.
     * `bind_group_member` is called for each serialized window id in order; it owns the
     * conflict/rule/workspace policy, calls `lt_grouped_add`, and sets `c->leaf` when the
     * member is accepted. Returns null on allocation failure. Caller owns the result. */
    using GroupBindFn = std::function<void(LayoutNode* leaf, Window win)>;
    [[nodiscard]] std::unique_ptr<LayoutNode> build_layout_tree(const SerializedNode& root, const GroupBindFn& bind);

    /* Persisted workspace key encode/decode (`w<id>` normal, `s<hidden_id>` special). */
    [[nodiscard]] std::string                 format_persist_workspace_key(const WorkspaceRef& ws);
    [[nodiscard]] bool                        parse_persist_workspace_key(std::string_view token, WorkspaceRef* out);
    [[nodiscard]] std::optional<WorkspaceRef> decode_persist_workspace_ref(std::string_view token, int require_registered_special);

} // namespace wm::bsp
