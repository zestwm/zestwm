/* Apply input { } / device { } using X11 helper programs (setxkbmap, xset, xinput). */
#include "config.hpp"
#include "log.hpp"
#include "util.hpp"

#include <chrono>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

    /* One-shot keyboard re-apply deadline; armed after initial apply in setup(). */
    std::optional<std::chrono::steady_clock::time_point> input_kb_reapply_deadline;

    /* Must exceed the autostart grace window (autostart.cpp sleeps 2s); switch to PropertyNotify if still flaky. */
    constexpr std::chrono::milliseconds kInputKbReapplyDelay{2500};

    [[nodiscard]] bool                  kb_settings_configured(void) noexcept {
        return !g_config.wm_input.kb_layout.empty() || !g_config.wm_input.kb_variant.empty() || !g_config.wm_input.kb_model.empty() || !g_config.wm_input.kb_options.empty() ||
            !g_config.wm_input.kb_rules.empty();
    }

    /* Fork/exec helper argv; log label on fork, wait, or non-zero exit failure. */
    [[nodiscard]] bool run_command_checked(const std::vector<std::string>& args, std::string_view label) noexcept {
        if (args.empty() || args[0].empty())
            return false;
        std::vector<char*> av;
        av.reserve(args.size() + 1U);
        for (const std::string& s : args)
            av.push_back(s.empty() ? nullptr : const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        const pid_t p = fork();
        if (p < 0) {
            wm::log::warn_and_log(std::string("zestwm: ") + std::string(label) + " fork failed: " + std::strerror(errno));
            return false;
        }
        if (p == 0) {
            setsid();
            execvp(av[0], av.data());
            _exit(127);
        }
        int status = 0;
        if (waitpid(p, &status, 0) != p) {
            wm::log::warn_and_log(std::string("zestwm: ") + std::string(label) + " waitpid failed: " + std::strerror(errno));
            return false;
        }
        if (!WIFEXITED(status)) {
            wm::log::warn_and_log(std::string("zestwm: ") + std::string(label) + " exited abnormally");
            return false;
        }
        const int code = WEXITSTATUS(status);
        if (code != 0) {
            wm::log::warn_and_log(std::string("zestwm: ") + std::string(label) + " failed with exit code " + std::to_string(code));
            return false;
        }
        return true;
    }

    void run_command(std::vector<std::string> args) {
        ignore_result(run_command_checked(args, args.empty() ? "command" : args[0]));
    }

    /* Arm one-shot setxkbmap re-apply after xdg autostart grace window. */
    void arm_input_kb_reapply(void) noexcept {
        if (!g_config.wm_input.input_block || !kb_settings_configured())
            return;
        input_kb_reapply_deadline = std::chrono::steady_clock::now() + kInputKbReapplyDelay;
    }

} // namespace

static std::string norm_lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s)
        o += static_cast<char>(std::tolower(c));
    return o;
}

static int xinput_id_from_line(const std::string& line) {
    size_t p = line.find("id=");
    if (p == std::string::npos)
        return -1;
    const char* q   = line.c_str() + p + 3;
    char*       end = nullptr;
    long        v   = strtol(q, &end, 10);
    if (end == q || v < 0 || v > 65535)
        return -1;
    return static_cast<int>(v);
}

static bool line_is_pointer_slave(const std::string& line) {
    std::string l = norm_lower(line);
    return l.find("slave") != std::string::npos && l.find("pointer") != std::string::npos;
}

static bool line_is_touchpad(const std::string& line) {
    return norm_lower(line).find("touchpad") != std::string::npos;
}

static void xinput_try_set_prop_int(int id, const char* prop, int v) {
    run_command({"xinput", "set-prop", std::to_string(id), prop, std::to_string(v)});
}

static void xinput_try_set_prop_float(int id, const char* prop, float v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.4f", static_cast<double>(v));
    run_command({"xinput", "set-prop", std::to_string(id), prop, buf});
}

/* Apply keyboard layout from g_config.wm_input via setxkbmap; returns false when the helper fails. */
[[nodiscard]] static bool apply_setxkbmap(void) noexcept {
    if (!kb_settings_configured())
        return true;
    std::vector<std::string> tok;
    tok.push_back("setxkbmap");
    if (!g_config.wm_input.kb_layout.empty()) {
        tok.push_back("-layout");
        tok.push_back(g_config.wm_input.kb_layout);
    }
    if (!g_config.wm_input.kb_variant.empty()) {
        tok.push_back("-variant");
        tok.push_back(g_config.wm_input.kb_variant);
    }
    if (!g_config.wm_input.kb_model.empty()) {
        tok.push_back("-model");
        tok.push_back(g_config.wm_input.kb_model);
    }
    if (!g_config.wm_input.kb_options.empty()) {
        tok.push_back("-option");
        tok.push_back(g_config.wm_input.kb_options);
    }
    if (!g_config.wm_input.kb_rules.empty()) {
        tok.push_back("-rules");
        tok.push_back(g_config.wm_input.kb_rules);
    }
    return run_command_checked(tok, "setxkbmap");
}

static void apply_xset_repeat(void) {
    if (!g_config.wm_input.repeat_delay_set && !g_config.wm_input.repeat_rate_set)
        return;
    int delay = g_config.wm_input.repeat_delay_ms;
    int rate  = g_config.wm_input.repeat_rate_hz;
    if (!g_config.wm_input.repeat_delay_set)
        delay = 660;
    if (!g_config.wm_input.repeat_rate_set)
        rate = 25;
    run_command({"xset", "r", "rate", std::to_string(delay), std::to_string(rate)});
}

typedef void (*xinput_line_fn)(int id, const std::string& line, void* ctx);

static void foreach_xinput_pointer_line(xinput_line_fn fn, void* ctx) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execlp("xinput", "xinput", "list", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(pipefd[1]);
    FILE* fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        waitpid(pid, nullptr, 0);
        return;
    }
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line_is_pointer_slave(line))
            continue;
        int id = xinput_id_from_line(line);
        if (id < 0)
            continue;
        fn(id, line, ctx);
    }
    fclose(fp);
    waitpid(pid, nullptr, 0);
}

struct sens_ctx {
    const InputDeviceConf* rule;
    bool                   global;
};

static void apply_sens_cb(int id, const std::string& line, void* vctx) {
    auto*       ctx  = static_cast<struct sens_ctx*>(vctx);
    std::string lown = norm_lower(line);
    if (ctx->global) {
        if (std::fabs(static_cast<double>(g_config.wm_input.sensitivity)) < 1e-6)
            return;
        xinput_try_set_prop_float(id, "libinput Accel Speed", g_config.wm_input.sensitivity);
        return;
    }
    const InputDeviceConf* d = ctx->rule;
    if (!d || d->name.empty() || !d->sensitivity_set)
        return;
    std::string pat = norm_lower(d->name);
    if (lown.find(pat) == std::string::npos)
        return;
    xinput_try_set_prop_float(id, "libinput Accel Speed", d->sensitivity);
}

static void apply_natural_cb(int id, const std::string& line, void*) {
    if (!g_config.wm_input.touch_natural_set)
        return;
    if (!line_is_touchpad(line))
        return;
    xinput_try_set_prop_int(id, "libinput Natural Scrolling Enabled", g_config.wm_input.touch_natural ? 1 : 0);
}

void wmconf_apply_input_settings(void) {
    if (!getenv("DISPLAY"))
        return;
    if (!g_config.wm_input.input_block && g_config.wm_input.devices.empty())
        return;

    if (g_config.wm_input.input_block) {
        ignore_result(apply_setxkbmap());
        arm_input_kb_reapply();
        apply_xset_repeat();

        if (g_config.wm_input.touch_natural_set)
            foreach_xinput_pointer_line(apply_natural_cb, nullptr);

        if (g_config.wm_input.sensitivity_set && std::fabs(static_cast<double>(g_config.wm_input.sensitivity)) > 1e-6) {
            struct sens_ctx c = {nullptr, true};
            foreach_xinput_pointer_line(apply_sens_cb, &c);
        }
    }
    for (const InputDeviceConf& d : g_config.wm_input.devices) {
        if (!d.sensitivity_set)
            continue;
        struct sens_ctx c = {&d, false};
        foreach_xinput_pointer_line(apply_sens_cb, &c);
    }
}

/* Run deferred setxkbmap once when the autostart grace deadline elapses. */
void wmconf_input_kb_reapply_poll(void) noexcept {
    if (!input_kb_reapply_deadline)
        return;
    if (std::chrono::steady_clock::now() < *input_kb_reapply_deadline)
        return;
    input_kb_reapply_deadline.reset();
    if (!getenv("DISPLAY"))
        return;
    ignore_result(apply_setxkbmap());
}

/* Cap poll timeout so deferred keyboard re-apply fires even with inotify (-1) blocking poll. */
int wmconf_input_kb_reapply_poll_ms_cap(int poll_ms) noexcept {
    if (!input_kb_reapply_deadline)
        return poll_ms;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*input_kb_reapply_deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0)
        return 0;
    const int cap = remaining > INT_MAX ? INT_MAX : static_cast<int>(remaining);
    if (poll_ms < 0)
        return cap;
    return cap < poll_ms ? cap : poll_ms;
}
