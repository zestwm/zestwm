/* Global WM state (defined in wm_state.cpp). */
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "x11/atoms.hpp"
#include "types.hpp"
#include "x11/backend.hpp"
#include "x11/color_utils.hpp"
#include "x11/cursor_utils.hpp"
#include "draw.hpp"

extern int                               screen;
extern int                               sw, sh;
extern int                               bh, th, lrpad;
extern unsigned int                      numlockmask;
extern int                               running;
extern int                               restart;
inline constexpr std::size_t             kCursorSlotCount = static_cast<std::size_t>(CursorKind::Last);
extern Cur*                              cursor[kCursorSlotCount];
extern std::vector<std::array<Clr, 3>>   scheme;
extern xcb_connection_t*                 xcb;
extern xcb_screen_t*                     xscreen;
extern xcb_visualtype_t*                 root_visual;
extern uint8_t                           root_depth;
extern xcb_cursor_context_t*             cursor_ctx;
extern std::unique_ptr<wm::draw::Canvas> canvas;
extern unsigned int                      canvas_font_height;
/* Monitor chain / last-focused: use `wm::state::mons_slot()`, `lastfocused_slot()`, or
 * `runtime_authority()` in hot paths; `build_runtime_state_root()` only at dispatch boundaries. */
extern Window                             root, wmcheckwin;

void                                      setclientstate(Client* c, long state);
void                                      setclientworkspaceprop(Client* c);
void                                      savezestwmstate(void);
void                                      restorezesttreestate(void);
void                                      savezesttreestate(void);
void                                      savezestspecialoverlaystate(void);
void                                      restorezestspecialoverlaystate(void);
void                                      savezestspecialhiddenidstate(void);
[[nodiscard]] std::optional<WorkspaceRef> tree_state_find_workspace_for_window(Window win);
[[nodiscard]] std::optional<WorkspaceRef> apply_workspace_from_persistence(Client* c, int workspace_set_by_rule);
void                                      restorezestselectionstate(void);
void                                      savezestselectionstate(void);
void                                      savezestlayoutstate(void);
void                                      savezestlayoutliststate(void);

/* Refresh root `_NET_NUMBER_OF_DESKTOPS`, `_NET_CURRENT_DESKTOP`, `_NET_DESKTOP_NAMES` from the workspace registry. */
void update_net_desktop_props(void);

/* Clear owned BSP roots on `m` (monitor teardown). */
void monitor_free_workspace_trees(Monitor* m);

/* Hidden-id bridge helpers for special workspace migration (slot-based stable ids 32..128). */
[[nodiscard]] std::optional<WorkspaceId>  workspace_hidden_id_for_special_tag(std::string_view tag);
[[nodiscard]] std::optional<WorkspaceRef> workspace_special_ref_from_hidden_id(WorkspaceId hidden_id);
[[nodiscard]] WorkspaceRef                workspace_normalize_special_ref_with_hidden_id(const WorkspaceRef& ws, std::optional<WorkspaceId> hidden_id = std::nullopt);

/* Non-zero iff this monitor is drawing the scratch overlay for `tag` (single hook for future “hidden desktop viewed”). */
[[nodiscard]] inline int monitor_special_overlay_shows_tag(const Monitor* m, std::string_view tag) noexcept {
    return m && m->special_overlay_open && m->special_overlay_tag == tag ? 1 : 0;
}
