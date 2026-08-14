/* Environment directive helpers for config parsing and deferred envd export.
 *
 * Responsibilities:
 * - parse `env = KEY, VALUE` and `envd = KEY, VALUE` directives;
 * - expand config variables in key/value fields before exporting to process env;
 * - defer optional DBus activation export keys until config load finalization.
 *
 * Runtime model:
 * - `apply_env_line` mutates current process environment immediately via `setenv`;
 * - `flush_envd_exports` performs deferred DBus activation export once per reload/startup.
 * - deferred export is best-effort: failure to spawn/exec the helper does not abort WM startup.
 *
 * Threading:
 * - intended for startup/reload single-threaded config parsing path;
 * - global queue state is not synchronized for concurrent reloads. */
#pragma once

#include "config.hpp"
#include <string_view>

namespace wm::config::env {

    /* Parse one env/envd assignment, expand both fields, and export to process environment.
     *
     * Input format:
     * - `KEY, VALUE` (first comma splits key from value).
     *
     * Behavior:
     * - always applies `setenv(KEY, VALUE, overwrite=1)`;
     * - when `also_dbus=true`, queues KEY for deferred DBus activation export.
     * - value is interpreted as raw environment payload (no shell quoting semantics).
     *
     * Errors:
     * - throws `std::runtime_error` on malformed assignments or failed `setenv`, with source context. */
    void apply_env_line(std::string_view val, bool also_dbus, const char* cfgpath_err, unsigned lineno, const ConfVars& conf_vars);

    /* Flush queued envd keys via detached `dbus-update-activation-environment` subprocess.
     *
     * Queue augmentation:
     * - appends required session baseline keys when present (DISPLAY, XAUTHORITY, ...).
     *
     * Execution model:
     * - uses a double-fork detached helper to avoid blocking WM runtime after reload/startup.
     * - logs warnings on helper spawn/exec anomalies and returns without throwing. */
    void flush_envd_exports();

    /* Clear queued envd keys, used during config reset/reload preparation.
     * Idempotent and noexcept by contract. */
    void clear_envd_exports() noexcept;

} /* namespace wm::config::env */
