/* BSP workspace-store API.
 *
 * Role:
 * - Encapsulate access to monitor workspace BSP roots (`WorkspaceRef -> unique_ptr<LayoutNode>`).
 * - Provide one object-level entry point for slot read/write/create operations used by arrange/state code.
 *
 * Scope:
 * - This class is a storage wrapper only: it does not sanitize, compact, or arrange trees.
 * - It intentionally delegates workspace validation/special-tag normalization to existing low-level helpers.
 *
 * Usage notes:
 * - `owned_slot(ws)` may create storage entries when the underlying helper materializes a slot.
 * - `read(ws)` is read-only intent and may return `nullptr` when no root exists for that workspace.
 */
#pragma once

#include "types.hpp"

#include <memory>
#include <string_view>

class BspWorkspaceStore {
  public:
    explicit BspWorkspaceStore(Monitor& monitor) noexcept;

    /* Writable owned root slot for workspace reference (`ws`).
     *
     * Contract:
     - Returns storage reference used by low-level workspace root map.
     - For invalid/unset refs, underlying helper may return sink storage slot.
     */
    std::unique_ptr<LayoutNode>& owned_slot(const WorkspaceRef& ws);
    /* Read current root pointer for workspace reference (`ws`) without assigning. */
    [[nodiscard]] LayoutNode* read(const WorkspaceRef& ws) const;
    /* True when monitor already owns a BSP root slot for this special tag. */
    [[nodiscard]] bool has_special_root_slot(std::string_view tag) const;
    /* Assign workspace root ownership explicitly (destroys previous root). */
    void set(const WorkspaceRef& ws, std::unique_ptr<LayoutNode> root);
    /* Steal owned root from slot (leaves nullptr). */
    [[nodiscard]] std::unique_ptr<LayoutNode> take(const WorkspaceRef& ws);
    /* Get or lazily create grouped root for a normal workspace id. */
    [[nodiscard]] LayoutNode* get_or_create_normal(WorkspaceId id);

  private:
    [[nodiscard]] bool has_root_slot(const WorkspaceRef& ws) const;
    Monitor&           m_;
};

/* Drop saved tree roots for a workspace on all monitors (non-persistent policy). */
void bsp_clear_workspace_tree_state(WorkspaceId id);
