/* Monitor allocation, teardown, and RandR geometry sync implementation. */
#include "monitor/monitor_lifecycle.hpp"

#include "bsp/workspace_store.hpp"
#include "client/client_lifecycle.hpp"
#include "config.hpp"
#include "draw/bar.hpp"
#include "intern.hpp"
#include "monitor/world_state.hpp"
#include "state/runtime_authority.hpp"
#include "state/wm_state_root.hpp"
#include "wm_state.hpp"
#include "x11/monitor_query.hpp"
#include "x11/wm_window.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace {

    [[nodiscard]] int rect_overlap_area(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) noexcept {
        const int x0 = std::max(ax, bx);
        const int y0 = std::max(ay, by);
        const int x1 = std::min(ax + aw, bx + bw);
        const int y1 = std::min(ay + ah, by + bh);
        if (x1 <= x0 || y1 <= y0)
            return 0;
        return (x1 - x0) * (y1 - y0);
    }

    /* Pick destination monitor for a client leaving a dying output: max geometry overlap, else first keep. */
    [[nodiscard]] Monitor* pick_rehome_monitor(const std::vector<Monitor*>& keep, Client* client) noexcept {
        if (keep.empty())
            return nullptr;
        if (!client)
            return keep.front();
        Monitor*  best      = keep.front();
        int       best_area = -1;
        const int cw        = client_outer_width(client);
        const int ch        = client_outer_height(client);
        for (Monitor* m : keep) {
            const int area = rect_overlap_area(client->x, client->y, cw, ch, m->mx, m->my, m->mw, m->mh);
            if (area > best_area) {
                best_area = area;
                best      = m;
            }
        }
        return best;
    }

    void rehome_clients_from_monitor(Monitor* src, const std::vector<Monitor*>& keep) noexcept {
        if (!src || keep.empty())
            return;
        const std::vector<Client*> moved(src->clients.begin(), src->clients.end());
        src->clients.clear();
        src->stack.clear();
        for (Client* client : moved) {
            if (!client)
                continue;
            Monitor* dst = pick_rehome_monitor(keep, client);
            if (!dst)
                dst = keep.front();
            client_unlink_stack(src, client);
            client_unlink(src, client);
            client->mon = dst;
            client_link(dst, client);
            client_link_stack(dst, client);
        }
    }

    [[nodiscard]] bool apply_monitor_rect(Monitor* m, const wm::x11::MonitorRect& r, int num, Monitor* current) noexcept {
        if (!m)
            return false;
        bool dirty = false;
        if (m->num != num || m->mx != r.x || m->my != r.y || m->mw != r.w || m->mh != r.h || m->output_name != r.name) {
            dirty          = true;
            m->num         = num;
            m->output_name = r.name;
            m->mx = m->wx = r.x;
            m->my = m->wy = r.y;
            m->mw = m->ww = r.w;
            m->mh = m->wh = r.h;
            updatebarpos(m, current);
        }
        return dirty;
    }

    /* X11/bar/BSP teardown for a monitor about to be destroyed (ownership already released). */
    void teardown_monitor_resources(Monitor* mon) {
        if (!mon)
            return;
        if (mon->barwin) {
            wm::x11::unmap_window(mon->barwin);
            wm::x11::destroy_window(mon->barwin);
        }
        for (const GroupbarSlot& slot : mon->groupbars) {
            if (!slot.win)
                continue;
            wm::x11::unmap_window(slot.win);
            wm::x11::destroy_window(slot.win);
        }
        mon->groupbars.clear();
        if (mon->confwin) {
            wm::x11::unmap_window(mon->confwin);
            wm::x11::destroy_window(mon->confwin);
        }
        monitor_free_workspace_trees(mon);
        mon->tree_world_viewed = {};
        MonitorWorldState(*mon).clear_overlay();
        if (mon->special_dimwin) {
            wm::x11::destroy_window(mon->special_dimwin);
            mon->special_dimwin = 0;
        }
    }

} // namespace

/* Tear down one monitor and drop it from the authority-owned vector. */
void monitor_cleanup(Monitor* mon) {
    if (!mon)
        return;
    auto& owners = wm::state::runtime_authority().monitors;
    auto  it     = std::find_if(owners.begin(), owners.end(), [mon](const std::unique_ptr<Monitor>& p) { return p.get() == mon; });
    if (it == owners.end()) {
        teardown_monitor_resources(mon);
        delete mon;
        return;
    }
    teardown_monitor_resources(mon);
    owners.erase(it);
}

/* Allocate and initialize a monitor (caller inserts into authority ownership). */
Monitor* createmon(void) {
    auto     owned       = std::make_unique<Monitor>();
    Monitor* m           = owned.get();
    size_t   startup_idx = 0;
    size_t   i;

    m->active_workspace_id = 1U;
    m->tx = m->tw                     = 0;
    m->th                             = 0;
    m->group_anchor                   = nullptr;
    const WorkspaceRef initial_viewed = WorkspaceRef::normal(kWorkspaceIdMin);
    static_cast<void>(BspWorkspaceStore(*m).get_or_create_normal(kWorkspaceIdMin));
    MonitorWorldState(*m).set_viewed(initial_viewed);
    MonitorWorldState(*m).clear_overlay();
    if (g_config.startup_layout) {
        for (i = 0; i < g_config.layouts.size(); i++) {
            if (g_config.layouts[i].arrange == g_config.startup_layout) {
                startup_idx = i;
                break;
            }
        }
    }
    m->layout_slots[0] = &g_config.layouts[startup_idx];
    m->layout_slots[1] = &g_config.layouts[(startup_idx + 1) % g_config.layouts.size()];
    m->layout_label    = g_config.layouts[startup_idx].symbol;
    /* Temporary: park in authority so createmon callers that only hold Monitor* keep ownership. */
    wm::state::runtime_authority().monitors.push_back(std::move(owned));
    return m;
}

/* Reconcile owned monitors with RandR rects by output_name; rehome clients on shrink. */
int updategeom(wm::state::MonitorState& monitors) {
    int                               dirty   = 0;
    Monitor*                          current = monitors.current;
    std::vector<wm::x11::MonitorRect> rects   = wm::x11::query_active_monitor_rects();

    if (rects.empty())
        rects.push_back(wm::x11::MonitorRect{.x = 0, .y = 0, .w = sw, .h = sh, .name = {}});

    if (rects.size() == 1U) {
        wm::x11::MonitorRect& primary = rects.front();
        primary.w                     = sw;
        primary.h                     = sh;
    }

    auto&                                 owners = wm::state::runtime_authority().monitors;
    std::vector<std::unique_ptr<Monitor>> pool   = std::move(owners);
    owners.clear();

    std::vector<bool>                     used(pool.size(), false);
    std::vector<std::unique_ptr<Monitor>> ordered;
    ordered.reserve(rects.size());

    auto take_at = [&](std::size_t idx) -> std::unique_ptr<Monitor> {
        used[idx] = true;
        return std::move(pool[idx]);
    };

    for (std::size_t ri = 0; ri < rects.size(); ++ri) {
        const wm::x11::MonitorRect& r = rects[ri];
        std::unique_ptr<Monitor>    chosen;

        if (!r.name.empty()) {
            for (std::size_t i = 0; i < pool.size(); ++i) {
                if (used[i] || !pool[i])
                    continue;
                if (pool[i]->output_name == r.name) {
                    chosen = take_at(i);
                    break;
                }
            }
            if (!chosen) {
                for (std::size_t i = 0; i < pool.size(); ++i) {
                    if (used[i] || !pool[i])
                        continue;
                    if (pool[i]->output_name.empty()) {
                        chosen = take_at(i);
                        break;
                    }
                }
            }
        } else {
            for (std::size_t i = 0; i < pool.size(); ++i) {
                if (used[i] || !pool[i])
                    continue;
                if (pool[i]->output_name.empty()) {
                    chosen = take_at(i);
                    break;
                }
            }
            if (!chosen) {
                for (std::size_t i = 0; i < pool.size(); ++i) {
                    if (!used[i] && pool[i]) {
                        chosen = take_at(i);
                        break;
                    }
                }
            }
        }
        if (!chosen) {
            /* createmon parks into owners; steal it back into local ordered. */
            Monitor* raw = createmon();
            chosen       = std::move(owners.back());
            owners.pop_back();
            static_cast<void>(raw);
            dirty = 1;
        }
        if (apply_monitor_rect(chosen.get(), r, static_cast<int>(ri), current))
            dirty = 1;
        ordered.push_back(std::move(chosen));
    }

    owners                           = std::move(ordered);
    const std::vector<Monitor*> keep = wm::state::all_monitors();

    for (std::size_t i = 0; i < pool.size(); ++i) {
        if (used[i] || !pool[i])
            continue;
        Monitor* extra = pool[i].get();
        if (extra == current)
            monitors.current = keep.empty() ? nullptr : keep.front();
        rehome_clients_from_monitor(extra, keep);
        if (current == extra)
            current = keep.empty() ? nullptr : keep.front();
        teardown_monitor_resources(extra);
        pool[i].reset();
        dirty = 1;
    }

    if (dirty)
        monitors.current = wintomon_from_fallback(root, current ? current : (keep.empty() ? nullptr : keep.front()));
    return dirty;
}
