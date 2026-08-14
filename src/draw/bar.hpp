/* Monitor bar, group tab, and config-error banner drawing API.
 *
 * Role:
 * - Tab/groupbar rendering, config banner, and workarea adjustment for dock struts.
 * - Owns groupbar offscreen canvas and chrome color state extracted from zestwm.cpp.
 */
#pragma once

#include "types.hpp"

#include <memory>

namespace wm::draw {
    class Canvas;
}

void          draw_bar_init_canvas(std::unique_ptr<wm::draw::Canvas> canvas, unsigned font_height);
void          draw_bar_resize_canvas(unsigned width, unsigned height);
void          draw_bar_shutdown();
void          draw_init_groupbar_chrome();

void          drawtab(Monitor* m);
void          updategroupbarwin(void);
void          updateconfigbannerwin(void);
void          drawconfigbanner(Monitor* m, Monitor* current);
void          updatebarpos(Monitor* m, Monitor* current);
void          showconfigerrorbanner(void);
GroupbarSlot* monitor_groupbar_slot_for_window(Monitor* m, Window w);
int           groupbar_thickness(void);

int           client_strut_partial(Client* c, long* left, long* right, long* top, long* bottom);
void          dock_merge_geometry_reserve(Client* c, Monitor* m, long* l, long* r, long* t, long* b);
