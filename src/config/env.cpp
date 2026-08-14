/* Environment directive helpers implementation.
 *
 * This module owns two distinct responsibilities:
 * 1) immediate process-environment mutation during config parse (`env`/`envd`);
 * 2) deferred DBus activation export of selected keys after parse completion.
 *
 * Design notes:
 * - parser failures are explicit (throw with source/line context);
 * - envd export is best-effort (warn/log, do not throw from helper path). */
#include "config/env.hpp"

#include "log.hpp"
#include "sys/spawn.hpp"

#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unordered_set>
#include <unistd.h>
#include <vector>

namespace wm::config::env {

    namespace {
        /* Deferred envd variable names collected during config parsing.
         * Vector preserves insertion order for deterministic helper arguments/logging. */
        std::vector<std::string> envd_keys;
        /* Membership set to keep `envd_keys` unique while preserving insertion order in vector. */
        std::unordered_set<std::string> envd_key_set;
        /* NOTE: Global mutable state assumes single-threaded config parsing/reload.
         * If concurrent reload is introduced, guard both containers with a mutex. */

        /* Return true when key already queued for envd export. */
        bool has_envd_key(const std::string& key) {
            return envd_key_set.contains(key);
        }

        /* Queue key exactly once in envd export list. */
        void queue_envd_key(const std::string& key) {
            if (has_envd_key(key))
                return;
            static_cast<void>(envd_key_set.insert(key));
            envd_keys.push_back(key);
        }

        /* Throw contextual parser error for invalid env/envd directive value.
         * Error payload intentionally mirrors other parser modules: `source:line: detail`. */
        [[noreturn]] void throw_env_error(const char* cfgpath_err, unsigned lineno, std::string_view detail) {
            std::string msg = "zestwm: ";
            msg += (cfgpath_err && *cfgpath_err) ? cfgpath_err : "?";
            msg += ":";
            msg += std::to_string(lineno);
            msg += ": ";
            msg += detail;
            throw std::runtime_error(msg);
        }

        /* Validate KEY token against POSIX-like environment variable identifier shape.
         * Accepted: `[A-Za-z_][A-Za-z0-9_]*`.
         * Rejected keys never reach `setenv`, keeping parser errors deterministic and explicit. */
        [[nodiscard]] bool is_valid_env_key(std::string_view key) noexcept {
            if (key.empty())
                return false;
            const unsigned char first = static_cast<unsigned char>(key[0]);
            if (!std::isalpha(first) && key[0] != '_')
                return false;
            for (std::size_t i = 1; i < key.size(); ++i) {
                const unsigned char ch = static_cast<unsigned char>(key[i]);
                if (!std::isalnum(ch) && key[i] != '_')
                    return false;
            }
            return true;
        }
    } // namespace

    /* Parse env/envd assignment, export to process env, and optionally queue envd variable key.
     * This is a parsing-path API and may throw on malformed input or failed setenv operations. */
    void apply_env_line(std::string_view val, bool also_dbus, const char* cfgpath_err, unsigned lineno, const ConfVars& conf_vars) {
        /* Split on the first comma: value segment may itself contain commas. */
        const size_t comma = val.find(',');
        if (comma == std::string::npos)
            throw_env_error(cfgpath_err, lineno, "env needs KEY, VALUE (comma-separated)");

        const std::string val_prefix(val.substr(0, comma));
        const std::string val_suffix(val.substr(comma + 1));
        std::string       env_key = wm::config::parse::trim(wm::config::expand::expand_all(wm::config::parse::trim(val_prefix), conf_vars));
        /* Value is passed to setenv verbatim after expansion/trim.
         * No shell quoting/parsing occurs (intentional: direct process environment mutation). */
        std::string env_val = wm::config::parse::trim(wm::config::expand::expand_all(wm::config::parse::trim(val_suffix), conf_vars));
        if (env_key.empty())
            throw_env_error(cfgpath_err, lineno, "env key is empty");
        if (!is_valid_env_key(env_key))
            throw_env_error(cfgpath_err, lineno, "env key is invalid (must match [A-Za-z_][A-Za-z0-9_]*)");
        if (::setenv(env_key.c_str(), env_val.c_str(), 1) != 0)
            throw_env_error(cfgpath_err, lineno, "setenv " + env_key + " failed: " + std::strerror(errno));

        /* Queue only the key for deferred DBus export; value is read by helper from process env. */
        if (also_dbus)
            queue_envd_key(env_key);
    }

    /* Spawn detached `dbus-update-activation-environment --all`.
     * Non-throwing best-effort path: startup/reload should continue even if helper fails. */
    void flush_envd_exports() {
        wm::log::append_log_line("zestwm: launching dbus-update-activation-environment --all");
        if (!envd_keys.empty()) {
            std::string keys_line = "zestwm: envd export keys:";
            for (const auto& key : envd_keys) {
                keys_line += " ";
                keys_line += key;
            }
            wm::log::append_log_line(keys_line);
        }

        /* First fork: parent waits for short-lived intermediate child.
         * Child performs second fork and detaches; this avoids long-running helper as WM child. */
        const pid_t mid = ::fork();
        if (mid < 0)
            return;
        if (mid != 0) {
            int status = 0;
            if (::waitpid(mid, &status, 0) < 0) {
                wm::log::warn_and_log(std::string("zestwm: waitpid for envd helper failed: ") + std::strerror(errno));
            } else if (!WIFEXITED(status) && !WIFSIGNALED(status)) {
                wm::log::warn_and_log("zestwm: dbus envd helper exited abnormally");
            }
            return;
        }

        /* Second fork: intermediate child exits immediately; grandchild becomes detached helper. */
        if (::fork() > 0)
            _exit(0);
        wm::sys::prepare_detached_child(nullptr);

        /* Build helper argv from owned strings first, then project stable char* pointers. */
        std::vector<std::string> args;
        args.reserve(2U);
        args.push_back("dbus-update-activation-environment");
        args.push_back("--all");

        /* `argv` points into `args` string buffers.
         * Safe because:
         * - `args` outlives `execvp` call in this process;
         * - on success, process image is replaced;
         * - on failure, buffers remain valid for fallback diagnostics before `_exit`. */
        std::vector<char*> argv;
        argv.reserve(args.size() + 1U);
        for (auto& arg : args)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        ::execvp("dbus-update-activation-environment", argv.data());
        wm::log::warn_and_log(std::string("zestwm: execvp dbus-update-activation-environment failed: ") + std::strerror(errno));
        _exit(127);
    }

    /* Drop all queued envd keys during config state reset. */
    void clear_envd_exports() noexcept {
        envd_keys.clear();
        envd_key_set.clear();
    }

} /* namespace wm::config::env */
