/* WM-bound BSP tree-state glue: build a live LayoutNode tree from a SerializedNode, and
 * persisted workspace-key encode/decode (`w<id>` / `s<hidden_id>`). Links with the WM. */
#include "bsp/tree_serialize.hpp"

#include "layout_tree.hpp"
#include "special_workspace_registry.hpp"
#include "wm_state.hpp"
#include "workspace_id.hpp"

#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace wm::bsp {
    namespace {
        /* Parse the `w<ID>` v1 workspace token form. */
        [[nodiscard]] bool parse_workspace_token_v1(std::string_view token, WorkspaceId* out_workspace_id) {
            if (!out_workspace_id || token.empty() || token[0] != 'w')
                return false;
            const std::expected<WorkspaceId, std::string> parsed_id = parse_workspace_id(token.substr(1U));
            if (!parsed_id)
                return false;
            *out_workspace_id = *parsed_id;
            return true;
        }
    } // namespace

    std::unique_ptr<LayoutNode> build_layout_tree(const SerializedNode& root, const GroupBindFn& bind) {
        if (root.is_split) {
            if (!root.first || !root.second)
                return nullptr;
            auto first = build_layout_tree(*root.first, bind);
            if (!first)
                return nullptr;
            auto second = build_layout_tree(*root.second, bind);
            if (!second)
                return nullptr;
            const float ratio = static_cast<float>(root.ratio_10000) / 10000.0f;
            return lt_new_split(root.axis, ratio, std::move(first), std::move(second));
        }

        auto leaf = lt_new_grouped();
        if (!leaf)
            return nullptr;
        leaf->grouped.groupmode = root.grouped.groupmode ? 1 : 0;
        for (const Window win : root.grouped.wins)
            bind(leaf.get(), win);
        for (std::size_t i = 0U; i < leaf->grouped.clients.size(); ++i) {
            const Client* c = leaf->grouped.clients[i];
            if (c && c->win == root.grouped.activewin) {
                leaf->grouped.active = i;
                break;
            }
        }
        return leaf;
    }

    std::string format_persist_workspace_key(const WorkspaceRef& ws) {
        if (ws.is_normal())
            return std::string("w") + std::to_string(ws.normal_id);
        if (ws.is_special()) {
            if (const std::optional<WorkspaceId> hidden_id = workspace_hidden_id_for_special_tag(ws.special_tag); hidden_id.has_value())
                return std::string("s") + std::to_string(*hidden_id);
            return "w0";
        }
        return "w0";
    }

    bool parse_persist_workspace_key(std::string_view token, WorkspaceRef* out) {
        if (!out || token.empty())
            return false;
        if (token[0] == 'w') {
            WorkspaceId id = 0U;
            if (!parse_workspace_token_v1(token, &id))
                return false;
            *out = WorkspaceRef::normal(id);
            return true;
        }
        if (token[0] == 's') {
            if (token.size() == 1U)
                return false;
            unsigned long parsed = 0UL;
            for (std::size_t i = 1U; i < token.size(); ++i) {
                const char ch = token[i];
                if (ch < '0' || ch > '9')
                    return false;
                parsed = parsed * 10UL + static_cast<unsigned long>(ch - '0');
                if (parsed > static_cast<unsigned long>(std::numeric_limits<WorkspaceId>::max()))
                    return false;
            }
            const WorkspaceId                 hidden_id   = static_cast<WorkspaceId>(parsed);
            const std::optional<WorkspaceRef> special_ref = workspace_special_ref_from_hidden_id(hidden_id);
            if (!special_ref.has_value())
                return false;
            *out = *special_ref;
            return true;
        }
        return false;
    }

    std::optional<WorkspaceRef> decode_persist_workspace_ref(std::string_view token, int require_registered_special) {
        WorkspaceRef assign_ws;
        if (!parse_persist_workspace_key(token, &assign_ws))
            return std::nullopt;
        assign_ws = workspace_normalize_special_ref_with_hidden_id(assign_ws);
        if (require_registered_special && assign_ws.is_special() && !special_workspace_registry_find_by_tag(assign_ws.special_tag))
            return std::nullopt;
        return assign_ws;
    }
} // namespace wm::bsp
