/* Runtime settings + config API (implemented across src/config/ modules).
 *
 * Config values live in `g_config` (`ConfigState`). Loader/watch helpers stay as free functions.
 */
#pragma once

#include "config/group_focus_policy.hpp"
#include "types.hpp"

#include <xcb/xcb.h>

#include <array>
#include <map>
#include <string>
#include <vector>

struct InputDeviceConf {
    std::string name;
    bool        repeat_delay_set = false;
    int         repeat_delay_ms  = 660;
    bool        repeat_rate_set  = false;
    int         repeat_rate_hz   = 25;
    bool        sensitivity_set  = false;
    float       sensitivity      = 0.f;
};

struct WmInputConf {
    bool                         input_block = false;
    std::string                  kb_layout;
    std::string                  kb_variant;
    std::string                  kb_model;
    std::string                  kb_options;
    std::string                  kb_rules;
    bool                         repeat_delay_set  = false;
    int                          repeat_delay_ms   = 660;
    bool                         repeat_rate_set   = false;
    int                          repeat_rate_hz    = 25;
    int                          follow_mouse      = 1;
    bool                         sensitivity_set   = false;
    float                        sensitivity       = 0.f;
    bool                         touch_natural_set = false;
    bool                         touch_natural     = false;
    std::vector<InputDeviceConf> devices;
};

struct WmMiscConf {
    std::string font_family;
    bool        focus_on_activate          = false;
    bool        mouse_move_focuses_monitor = true;
    std::string background_color;
};

using ConfVars = std::map<std::string, std::string, std::less<>>;

/* Single in-memory config blob; `wmconf_free` resets via default construction. */
struct ConfigState {
    WmInputConf  wm_input{};
    WmMiscConf   wm_misc{};

    unsigned int borderpx        = 1;
    unsigned int snap            = 32;
    int          snap_enabled    = 1;
    int          resizehints     = 1;
    int          lockfullscreen  = 0;
    int          refreshrate     = 400;
    unsigned int gaps_in         = 0;
    double       activeopacity   = 1.0;
    double       inactiveopacity = 1.0;
    /* `decoration.dim_special`: dim strength behind an open special overlay [0.0–1.0]. */
    double dim_special               = 0.2;
    void (*startup_layout)(Monitor*) = nullptr;

    int                                     group_auto_group               = 1;
    int                                     dwindle_preserve_split         = 0;
    int                                     dwindle_force_split            = 0;
    int                                     dwindle_use_active_for_splits  = 1;
    float                                   dwindle_default_split_ratio    = 1.0f;
    float                                   dwindle_split_width_multiplier = 1.0f;
    int                                     groupbar_enabled               = 1;
    int                                     groupbar_render_titles         = 1;
    int                                     group_insert_after_current     = 1;
    wm::config::GroupFocusRemovedPolicy     group_focus_removed_window     = wm::config::GroupFocusRemovedPolicy::History;
    int                                     group_drag_into_group          = 1;
    int                                     group_drag_out_of_group        = 1;
    int                                     group_merge_groups_on_drag     = 1;
    int                                     groupbar_position              = 0; /* 0=top 1=left 2=right 3=bottom */
    int                                     groupbar_indicator_height      = 3;
    int                                     groupbar_indicator_gap         = 0;
    int                                     groupbar_indicator_position    = 3; /* 0=top 1=left 2=right 3=bottom */

    std::string                             active_border_color;
    std::string                             inactive_border_color;
    std::string                             group_border_active_color;
    std::string                             group_border_inactive_color;
    std::string                             groupbar_col_active;
    std::string                             groupbar_col_inactive;
    std::string                             groupbar_col_background;
    std::string                             groupbar_font_family;
    int                                     groupbar_font_size = 0;

    unsigned int                            modkey = Mod1Mask;
    std::vector<std::array<std::string, 3>> colors;
    std::vector<Layout>                     layouts;
    std::vector<Key>                        keys;
    std::vector<Button>                     buttons;
    std::vector<std::string>                exec_once;
    /* $name key/value pairs for config-time expansion. */
    ConfVars conf_vars;

    /* Restore defaults; containers clear via assignment. */
    void reset() noexcept {
        *this = ConfigState{};
    }
};

extern ConfigState g_config;

void               wmconf_load(const char* path);
void               wmconf_free(void);
/* Run exec-once and XDG autostart once per X session (skipped on WM reload via `--reload`). */
void wmconf_autostart(xcb_connection_t* xc);
/* Apply input { } from config (setxkbmap, xset, xinput); call once after X is usable. */
void wmconf_apply_input_settings(void);
/* One-shot setxkbmap re-apply after xdg autostart grace; call from event loop. */
void wmconf_input_kb_reapply_poll(void) noexcept;
/* Lower poll timeout when keyboard re-apply deadline is pending. */
[[nodiscard]] int wmconf_input_kb_reapply_poll_ms_cap(int poll_ms) noexcept;
/* Returns last config parse error message, or nullptr when config loaded cleanly. */
const char* wmconf_last_error(void);
/* Returns canonical list of loaded config files (main + source includes). */
const std::vector<std::string>& wmconf_loaded_files(void) noexcept;

/* Watch loaded config paths for changes (inotify on parents, else mtime poll); raises SIGHUP to reload. */
void              wmconf_watch_init(void);
void              wmconf_watch_maybe_reload(void);
[[nodiscard]] int wmconf_watch_inotify_fd(void) noexcept;
[[nodiscard]] int wmconf_watch_poll_timeout_ms(void) noexcept;
void              wmconf_watch_shutdown(void) noexcept;
