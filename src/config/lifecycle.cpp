/* Config lifecycle helpers implementation (cleanup/autostart). */
#include "config.hpp"

#include "config/parse/window_rule.hpp"
#include "config/env.hpp"
#include "autostart.hpp"
#include "sys/spawn.hpp"
#include "workspace_registry.hpp"

/* Reset config-owned runtime containers and restore default in-memory values. */
void wmconf_free(void) {
    /* NOTE: assumes single-threaded startup/reload; g_config is not mutex-protected.
     * Full reset: unset keys return to defaults on the following wmconf_load parse. */
    g_config.reset();
    wm::config::parse::clear_window_rules();
    workspace_registry_clear();
    wm::config::env::clear_envd_exports();
}

/* Spawn all exec-once commands in detached children after WM setup completes. */
void wmconf_autostart(xcb_connection_t* xc) {
    for (const std::string& cmd : g_config.exec_once) {
        if (cmd.empty()) [[unlikely]]
            continue;
        wm::sys::spawn_detached_shell(xc, cmd);
    }
    /* Launch XDG autostart pipeline (system + user .desktop entries) after WM init. */
    run_xdg_autostart(xc);
}
