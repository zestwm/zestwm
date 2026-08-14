/* BSP tree-ops API.
 *
 * Role:
 * - Host low-risk BSP helpers extracted from `zestwm.cpp`.
 * - Keep split decision/rewire rules in one module while monitor/workspace ownership
 *   remains in caller-side orchestration.
 *
 * Scope:
 * - Includes traversal helpers and local topology operations on `LayoutNode`.
 * - No monitor/world-state mutation.
 * - No persistence/registry coupling and no direct global-runtime reads.
 *
 * Invariants:
 * - Traversal order is stable: depth-first, left subtree before right subtree.
 * - Decision helpers are deterministic for identical input parameters.
 * - `insert_split` updates only local BSP links; caller handles root replacement.
 * - grouped-leaf constructors return null on allocation/append failure.
 */
#pragma once

#include "types.hpp"

#include <memory>
#include <optional>
#include <utility>

class BspTreeOps {
  public:
    using SplitAxisFn = SplitAxis (*)(LayoutNode*, Client*);
    using NewFirstFn  = int (*)(LayoutNode*, SplitAxis, Client*);
    using RatioFn     = float (*)();
    /* Outcome of insert_split: ok + optional new root when leaf was workspace root. */
    struct InsertSplitResult {
        bool                        ok{false};
        std::unique_ptr<LayoutNode> new_root{};
    };
    /* Return first grouped leaf containing at least one client in this subtree.
     *
     * Contract:
     * - `node` may be null.
     * - Returned pointer is borrowed and remains owned by caller-managed tree storage.
     *
     * Traversal:
     * - Depth-first, left subtree first (recursive implementation).
     *
     * Runtime note:
     * - Typical BSP depth in this WM is small; recursion is expected stack-safe in
     *   normal layouts.
     * - If future workloads show deep nesting/stack pressure in profiling, this can
     *   be replaced with bounded iterative DFS without changing semantics.
     */
    static LayoutNode* first_grouped(LayoutNode* node) noexcept;
    /* Return the leftmost grouped leaf under this subtree.
     *
     * Contract:
     * - Walk follows only `split.first` edges until a non-split node is reached.
     * - Returns null when traversal ends on null or on a non-grouped node.
     *
     * Usage:
     * - Fast deterministic anchor selection for callers that need "first lane" behavior
     *   without full DFS.
     */
    static LayoutNode* leftmost_grouped_leaf(LayoutNode* node) noexcept;
    /* Return first tab-anchor-eligible grouped leaf in this subtree.
     *
     * Contract:
     * - `node` may be null.
     * - Returned pointer is borrowed and valid while tree topology stays unchanged.
     *
     * Traversal:
     * - Depth-first, left subtree first (recursive implementation).
     *
     * Eligibility:
     * - `groupmode` enabled with at least one member, or
     * - grouped mode disabled with at least two members.
     *
     * Runtime note:
     * - Same recursion-depth considerations as `first_grouped(...)`.
     */
    static LayoutNode* first_tab_anchor(LayoutNode* node) noexcept;
    /* Infer split axis for insertion.
     *
     * Decision order:
     * 1) Use explicit preselect direction (`l/r/u/d/t/b`) when valid.
     * 2) Use fallback client geometry if it belongs to `leaf`.
     * 3) Use grouped-active slot (clamped when active index is stale).
     * 4) Default to vertical when no geometry reference is available.
     *
     * Design note:
     * - The `int preselect_dir` parameter is an ABI boundary value.
     *   Implementation converts it immediately to typed `DirectionKey`.
     */
    static SplitAxis infer_insert_split_axis(LayoutNode* leaf, Client* fallback, int preselect_dir) noexcept;
    /* Decide whether the new split node should be placed as first child during insertion.
     *
     * Decision order:
     * 1) Preselect direction (`l/u/t` -> first, `r/d/b` -> second).
     * 2) Explicit dwindle force policy (`force_split`: 1 -> first, 2 -> second).
     * 3) Pointer bias versus fallback center on selected axis.
     * 4) Grouped-leaf fallback heuristic (`active == 0` -> first).
     * 5) Stable default -> second.
     *
     * Contract:
     * - Return domain is `{0,1}` for C-interop with existing split constructors.
     * - `pointer_pos` is optional to keep this helper side-effect-free and testable.
     */
    static int compute_insert_new_first(LayoutNode* leaf, SplitAxis axis, Client* fallback, int preselect_dir, int force_split,
                                        std::optional<std::pair<int, int>> pointer_pos) noexcept;
    /* Build split between `leaf` and owned `newleaf`, stealing `leaf` from parent or `root_take`.
     *
     * When `leaf` has a parent, steals it and attaches the new split in its place (`new_root` empty).
     * When `leaf` is root, `root_take` must own that root; `new_root` holds the split for handoff.
     */
    [[nodiscard]] static InsertSplitResult insert_split(LayoutNode* leaf, std::unique_ptr<LayoutNode> newleaf, std::unique_ptr<LayoutNode> root_take, SplitAxis axis, float ratio,
                                                        int new_first) noexcept;
    /* Policy-driven split insert; on failure drops `newleaf`. */
    [[nodiscard]] static InsertSplitResult insert_split_with_policy(LayoutNode* leaf, std::unique_ptr<LayoutNode> newleaf, std::unique_ptr<LayoutNode> root_take, Client* fallback,
                                                                    SplitAxisFn split_axis, NewFirstFn new_first, RatioFn default_ratio) noexcept;
    /* Allocate a grouped leaf and append `client` as first occupant. */
    [[nodiscard]] static std::unique_ptr<LayoutNode> make_grouped_leaf_with_client(Client* client) noexcept;
    /* Append `client` to grouped `leaf` and sync `client->leaf` on success.
     *
     * Returns:
     * - `true` only when both grouped insertion and back-reference update succeed.
     * - `false` for invalid leaf/type/client or grouped append failure.
     */
    [[nodiscard]] static bool add_client_to_grouped_leaf(LayoutNode* leaf, Client* client) noexcept;
    /* Insert `client` into grouped `leaf` using current-anchor policy.
     *
     * Notes:
     * - `insert_after_current` is semantic bool to avoid C-style int flag ambiguity.
     * - Uses underlying grouped API int flag (`1`/`0`) only at the narrow boundary.
     */
    [[nodiscard]] static bool add_client_to_grouped_leaf_insert(LayoutNode* leaf, Client* client, bool insert_after_current) noexcept;
};
