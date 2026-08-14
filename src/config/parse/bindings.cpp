/* Config bind/button parser helper implementation. */
#include "config/parse/bindings.hpp"

#include "actions.hpp"
#include "actions/workspace.hpp"
#include "config/parse/action.hpp"
#include "config/parse/common.hpp"
#include "config/parse/expand.hpp"
#include "config/parse/utils.hpp"
#include "config/parse/values.hpp"
#include "log.hpp"

#include <X11/XF86keysym.h>
#include <X11/keysym.h>

#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <strings.h>

namespace wm::config::parse {

    /* Split modifier list on common separators used in config bind syntax. */
    [[nodiscard]] static std::vector<std::string> split_mod_tokens(std::string_view s) {
        std::vector<std::string> out;
        std::string              cur;
        out.reserve(4);
        for (char ch : s) {
            const unsigned char uc = static_cast<unsigned char>(ch);
            if (ch == '+' || ch == '_' || ch == '|' || uc == ' ' || uc == '\t') {
                std::string t = trim(cur);
                if (!t.empty())
                    out.push_back(t);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        std::string t = trim(cur);
        if (!t.empty())
            out.push_back(t);
        return out;
    }

    /* Convert one normalized modifier token to X11 mask bit. */
    [[nodiscard]] static bool mod_token_bit(std::string_view token, unsigned int* mask_out) {
        std::string upper;
        upper.reserve(token.size());

        for (char c : token)
            upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper == "SUPER" || upper == "MOD4" || upper == "WIN") {
            *mask_out = Mod4Mask;
            return true;
        }
        if (upper == "ALT" || upper == "MOD1") {
            *mask_out = Mod1Mask;
            return true;
        }
        if (upper == "SHIFT") {
            *mask_out = ShiftMask;
            return true;
        }
        if (upper == "CTRL" || upper == "CONTROL") {
            *mask_out = ControlMask;
            return true;
        }
        if (upper == "MOD2") {
            *mask_out = Mod2Mask;
            return true;
        }
        if (upper == "MOD3") {
            *mask_out = Mod3Mask;
            return true;
        }
        if (upper == "MOD5") {
            *mask_out = Mod5Mask;
            return true;
        }
        if (upper == "ISO_LEVEL3_SHIFT" || upper == "ALTGR") {
            *mask_out = Mod5Mask;
            return true;
        }
        if (upper == "ISO_LEVEL5_SHIFT") {
            /* X11 mapping is layout-dependent; keep a stable default mapping. */
            *mask_out = Mod3Mask;
            return true;
        }
        if (upper == "MOD") {
            /* Niri-style dynamic Mod key; on zestwm/X11 use Super default. */
            *mask_out = Mod4Mask;
            return true;
        }
        if (upper == "0" || upper == "NONE") {
            *mask_out = 0;
            return true;
        }
        return false;
    }

    /* Parse full modifier field and report first unknown token when present. */
    [[nodiscard]] bool parse_mods_checked(std::string_view s, unsigned int* mask_out, std::string* bad_token_out) {
        *mask_out = 0;
        for (const auto& token : split_mod_tokens(s)) {
            unsigned int bit;

            if (token.empty())
                continue;
            if (!mod_token_bit(token, &bit)) {
                if (bad_token_out)
                    *bad_token_out = token;
                return false;
            }
            *mask_out |= bit;
        }
        return true;
    }

    /* Decode key token into keycode/keysym pair accepted by bind parser. */
    [[nodiscard]] bool keysym_parse_bind(std::string_view raw, uint8_t* keycode_out, KeySym* keysym_out) {
        std::string s = trim(std::string(raw));

        *keycode_out = 0;
        *keysym_out  = static_cast<KeySym>(0);
        if (s.empty())
            return false;
        if (s.size() >= 6 && strncasecmp(s.c_str(), "code:", 5) == 0) {
            const auto parsed = wm::config::values::parse_uint_val(std::string_view(s).substr(5), 10);
            if (!parsed || *parsed < 8 || *parsed > 255)
                return false;
            *keycode_out = static_cast<uint8_t>(*parsed);
            return true;
        }
        if (s.size() == 1) {
            *keysym_out = static_cast<KeySym>(static_cast<unsigned char>(s[0]));
            return true;
        }
        if ((s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) || s[0] == '#') {
            const std::string_view token(s.c_str() + (s[0] == '#' ? 1 : 0));
            const auto             parsed = wm::config::values::parse_uint_val(token, 0);
            if (parsed) {
                *keysym_out = static_cast<KeySym>(*parsed);
                return true;
            }
        }
        static const struct {
            const char* name;
            KeySym      keysym;
        } tab[] = {
            {"Return", XK_Return},
            {"Escape", XK_Escape},
            {"Tab", XK_Tab},
            {"space", XK_space},
            {"BackSpace", XK_BackSpace},
            {"Delete", XK_Delete},
            {"Home", XK_Home},
            {"End", XK_End},
            {"Left", XK_Left},
            {"Right", XK_Right},
            {"Up", XK_Up},
            {"Down", XK_Down},
            {"Page_Up", XK_Page_Up},
            {"Page_Down", XK_Page_Down},
            {"Insert", XK_Insert},
            {"Print", XK_Print},
            {"PrintScreen", XK_Print},
            {"SysRq", XK_Sys_Req},
            {"SysReq", XK_Sys_Req},
            {"Menu", XK_Menu},
            {"comma", XK_comma},
            {"period", XK_period},
            {"slash", XK_slash},
            {"grave", XK_grave},
            {"minus", XK_minus},
            {"equal", XK_equal},
            {"bracketleft", XK_bracketleft},
            {"bracketright", XK_bracketright},
            {"semicolon", XK_semicolon},
            {"apostrophe", XK_apostrophe},
            {"F1", XK_F1},
            {"F2", XK_F2},
            {"F3", XK_F3},
            {"F4", XK_F4},
            {"F5", XK_F5},
            {"F6", XK_F6},
            {"F7", XK_F7},
            {"F8", XK_F8},
            {"F9", XK_F9},
            {"F10", XK_F10},
            {"F11", XK_F11},
            {"F12", XK_F12},
            {"XF86AudioRaiseVolume", XF86XK_AudioRaiseVolume},
            {"XF86AudioLowerVolume", XF86XK_AudioLowerVolume},
            {"XF86AudioMute", XF86XK_AudioMute},
            {"XF86AudioMicMute", XF86XK_AudioMicMute},
            {"XF86MonBrightnessUp", XF86XK_MonBrightnessUp},
            {"XF86MonBrightnessDown", XF86XK_MonBrightnessDown},
            {"XF86AudioNext", XF86XK_AudioNext},
            {"XF86AudioPause", XF86XK_AudioPause},
            {"XF86AudioPlay", XF86XK_AudioPlay},
            {"XF86AudioPrev", XF86XK_AudioPrev},
        };
        for (const auto& entry : tab) {
            if (strcasecmp(s.c_str(), entry.name) == 0) {
                *keysym_out = entry.keysym;
                return true;
            }
        }
        if (s.size() == 2 && s[0] == 'F' && std::isdigit(static_cast<unsigned char>(s[1]))) {
            const int n = s[1] - '0';
            if (n >= 1 && n <= 9) {
                *keysym_out = static_cast<KeySym>(XK_F1 + (n - 1));
                return true;
            }
        }
        if (s.size() == 1 && std::isdigit(static_cast<unsigned char>(s[0]))) {
            *keysym_out = static_cast<KeySym>(XK_1 + (s[0] - '1'));
            return true;
        }
        return false;
    }

    /* Resolve action dispatcher function from normalized config action name. */
    [[nodiscard]] KeyFn func_by_name_try(std::string_view name) {
        struct NamedAction {
            std::string_view name;
            KeyFn            fn;
        };
        static constexpr std::array<NamedAction, 40> kActions = {{
            {"spawn", spawn},
            {"exec", spawn},
            {"layoutmsg", layoutmsg},
            {"quit", quit},
            {"cyclefocus", cyclefocus},
            {"splitratio", splitratio},
            {"view", viewworkspace},
            {"workspace", viewworkspace},
            {"killclient", killclient},
            {"killactive", killclient},
            {"setlayout", setlayout},
            {"cyclelayout", cyclelayout},
            {"togglefloating", togglefloating},
            {"togglefullscreen", togglefullscreen},
            {"fullscreen", togglefullscreen},
            {"movetoworkspace", movetoworkspaceid},
            {"movetoworkspacesilent", movetoworkspacesilent},
            {"togglespecialworkspace", togglespecialworkspace},
            {"focusmonitor", focusmonitor},
            {"movetomonitor", movetomonitor},
            {"movegroup", movegroup},
            {"moveoutofgroup", moveoutofgroup},
            {"movewindoworgroup", movewindoworgroup},
            {"sendtogroup", sendtogroup},
            {"groupmode", groupmode},
            {"focusurgent", focusurgent},
            {"focussplit", focussplit},
            {"cyclegroup", cyclegroup},
            {"focusgroup", focusgroup},
            {"movemouse", movemouse},
            {"resizemouse", resizemouse},
            {"movefocus", movefocus},
            {"swapwindow", swapwindow},
            {"bringactivetotop", bringactivetotop},
            {"cyclenext", cyclenext},
            {"cycleprev", cycleprev},
            {"changegroupactive", changegroupactive},
            {"togglegroup", togglegroup},
            {"movetoworkspaceid", movetoworkspaceid},
        }};
        for (const auto& entry : kActions) {
            if (entry.name == name)
                return entry.fn;
        }
        return nullptr;
    }

    /* Emit parser warning with source context through shared logger. */
    static void warn_bind(std::string_view path, unsigned lineno, std::string_view detail) {
        const std::string_view source = path.empty() ? std::string_view("?") : path;
        std::string            msg    = "zestwm: ";
        msg.append(source);
        msg += ":";
        msg += std::to_string(lineno);
        msg += ": ";
        msg.append(detail);
        wm::log::warn_and_log(msg);
    }

    /* Parse niri-style `Mod+Key` token into modifier and key parts.
     *
     * The key is the last `+`-separated segment; everything before it is treated as modifiers.
     */
    [[nodiscard]] static bool split_hotkey_token(std::string_view token, std::string* mods_out, std::string* key_out) {
        const std::string t = trim(std::string(token));
        if (t.empty())
            return false;
        const std::size_t plus = t.rfind('+');
        if (plus == std::string::npos) {
            *mods_out = "";
            *key_out  = t;
            return !key_out->empty();
        }
        *mods_out = trim(t.substr(0, plus));
        *key_out  = trim(t.substr(plus + 1U));
        return !key_out->empty();
    }

    [[nodiscard]] static bool eq_ci(std::string_view a, std::string_view b) {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    /* Parse `true|false|1|0|on|off|yes|no` property values. */
    [[nodiscard]] static std::optional<bool> parse_bool_property(std::string_view value) {
        const std::string v = trim(std::string(value));
        if (eq_ci(v, "true") || eq_ci(v, "1") || eq_ci(v, "on") || eq_ci(v, "yes"))
            return true;
        if (eq_ci(v, "false") || eq_ci(v, "0") || eq_ci(v, "off") || eq_ci(v, "no"))
            return false;
        return std::nullopt;
    }

    enum class NiriPointerKind : unsigned char {
        MouseButton,
        Wheel,
        Touchpad,
    };

    struct NiriPointerToken {
        NiriPointerKind kind;
        unsigned int    button;
    };

    [[nodiscard]] static std::optional<NiriPointerToken> niri_pointer_token(std::string_view key) {
        if (eq_ci(key, "MouseLeft"))
            return NiriPointerToken{NiriPointerKind::MouseButton, 1U};
        if (eq_ci(key, "MouseRight"))
            return NiriPointerToken{NiriPointerKind::MouseButton, 3U};
        if (eq_ci(key, "MouseMiddle"))
            return NiriPointerToken{NiriPointerKind::MouseButton, 2U};
        if (eq_ci(key, "MouseForward"))
            return NiriPointerToken{NiriPointerKind::MouseButton, 9U};
        if (eq_ci(key, "MouseBack"))
            return NiriPointerToken{NiriPointerKind::MouseButton, 8U};
        if (eq_ci(key, "WheelScrollUp"))
            return NiriPointerToken{NiriPointerKind::Wheel, 4U};
        if (eq_ci(key, "WheelScrollDown"))
            return NiriPointerToken{NiriPointerKind::Wheel, 5U};
        if (eq_ci(key, "WheelScrollLeft"))
            return NiriPointerToken{NiriPointerKind::Wheel, 6U};
        if (eq_ci(key, "WheelScrollRight"))
            return NiriPointerToken{NiriPointerKind::Wheel, 7U};
        if (eq_ci(key, "TouchpadScrollUp"))
            return NiriPointerToken{NiriPointerKind::Touchpad, 4U};
        if (eq_ci(key, "TouchpadScrollDown"))
            return NiriPointerToken{NiriPointerKind::Touchpad, 5U};
        if (eq_ci(key, "TouchpadScrollLeft"))
            return NiriPointerToken{NiriPointerKind::Touchpad, 6U};
        if (eq_ci(key, "TouchpadScrollRight"))
            return NiriPointerToken{NiriPointerKind::Touchpad, 7U};
        return std::nullopt;
    }

    static void append_niri_pointer_bind(BindingsContext ctx, unsigned int modmask, unsigned int button, unsigned int flags, KeyFn fn,
                                         std::shared_ptr<const ActionCommand> command) {
        /* Match niri UX intent (operate on focused window) across root/client clicks. */
        ctx.buttons.push_back(Button{static_cast<unsigned>(ClickTarget::RootWindow), modmask, button, flags, fn, command});
        ctx.buttons.push_back(Button{static_cast<unsigned>(ClickTarget::ClientWindow), modmask, button, flags, fn, std::move(command)});
    }

    /* Legacy bind/button line parsers removed: `binds { ... }` is now canonical. */

    /* Parse one niri-style bind entry:
     *   Mod+Key [repeat=false] [cooldown-ms=500] { action [args...]; }
     */
    static void parse_niri_bind_entry(BindingsContext ctx, std::string_view line) {
        const std::size_t brace_open  = line.find('{');
        const std::size_t brace_close = line.rfind('}');
        if (brace_open == std::string::npos || brace_close == std::string::npos || brace_close <= brace_open) {
            warn_bind(ctx.path, ctx.lineno, "binds: expected '<hotkey> [props] { action ...; }'; skipping");
            return;
        }
        const std::string left  = trim(std::string(line.substr(0, brace_open)));
        std::string       right = trim(std::string(line.substr(brace_open + 1U, brace_close - brace_open - 1U)));
        if (left.empty() || right.empty()) {
            warn_bind(ctx.path, ctx.lineno, "binds: empty hotkey or action body; skipping");
            return;
        }
        if (!right.empty() && right.back() == ';')
            right.pop_back();
        right = trim(std::move(right));
        if (right.empty()) {
            warn_bind(ctx.path, ctx.lineno, "binds: empty action body; skipping");
            return;
        }

        std::istringstream left_iss(left);
        std::string        hotkey;
        left_iss >> hotkey;
        if (hotkey.empty()) {
            warn_bind(ctx.path, ctx.lineno, "binds: missing hotkey token; skipping");
            return;
        }

        unsigned int flags       = BindFlagRepeat; /* niri-compatible default: repeats enabled */
        unsigned int cooldown_ms = 0U;
        for (std::string prop; left_iss >> prop;) {
            const std::size_t eq = prop.find('=');
            if (eq == std::string::npos) {
                warn_bind(ctx.path, ctx.lineno, "binds: invalid property '" + prop + "' (expected key=value); skipping");
                return;
            }
            const std::string key = trim(prop.substr(0, eq));
            const std::string val = trim(prop.substr(eq + 1U));
            if (eq_ci(key, "repeat")) {
                const auto b = parse_bool_property(val);
                if (!b) {
                    warn_bind(ctx.path, ctx.lineno, "binds: invalid repeat value '" + val + "'; skipping");
                    return;
                }
                if (*b)
                    flags |= BindFlagRepeat;
                else
                    flags &= ~BindFlagRepeat;
                continue;
            }
            if (eq_ci(key, "cooldown-ms")) {
                const auto parsed = values::parse_uint_val(val, 10);
                if (!parsed || *parsed > std::numeric_limits<unsigned int>::max()) {
                    warn_bind(ctx.path, ctx.lineno, "binds: invalid cooldown-ms '" + val + "'; skipping");
                    return;
                }
                cooldown_ms = *parsed;
                continue;
            }
            if (eq_ci(key, "allow-when-locked") || eq_ci(key, "allow-inhibiting")) {
                const auto b = parse_bool_property(val);
                if (!b) {
                    warn_bind(ctx.path, ctx.lineno, "binds: invalid " + key + " value '" + val + "'; skipping");
                    return;
                }
                /* Parsed for config compatibility; X11 backend currently ignores it. */
                continue;
            }
            if (eq_ci(key, "hotkey-overlay-title")) {
                /* Niri UI metadata; parsed as no-op for compatibility. */
                continue;
            }
            warn_bind(ctx.path, ctx.lineno, "binds: unsupported property '" + key + "'; skipping");
            return;
        }

        std::string mods_tok;
        std::string key_tok;
        if (!split_hotkey_token(hotkey, &mods_tok, &key_tok)) {
            warn_bind(ctx.path, ctx.lineno, "binds: invalid hotkey '" + hotkey + "'; skipping");
            return;
        }
        unsigned int modmask = 0U;
        std::string  badmod;
        if (!parse_mods_checked(mods_tok, &modmask, &badmod)) {
            warn_bind(ctx.path, ctx.lineno, "binds: unknown modifier '" + badmod + "' in '" + mods_tok + "'; skipping");
            return;
        }
        std::istringstream action_iss(right);
        std::string        action;
        action_iss >> action;
        if (action.empty()) {
            warn_bind(ctx.path, ctx.lineno, "binds: missing action in body; skipping");
            return;
        }
        KeyFn fn = func_by_name_try(action);
        if (!fn) {
            warn_bind(ctx.path, ctx.lineno, "binds: unknown action '" + action + "'; skipping");
            return;
        }
        std::string argstr;
        std::getline(action_iss, argstr);
        argstr                = trim(std::move(argstr));
        argstr                = wm::config::expand::expand_all(argstr, ctx.conf_vars);
        const auto parsed_cmd = parse_action_command(fn, argstr);
        if (!parsed_cmd) {
            warn_bind(ctx.path, ctx.lineno, "binds: invalid args '" + argstr + "': " + parsed_cmd.error() + "; skipping");
            return;
        }
        auto command = std::make_shared<ActionCommand>(std::move(*parsed_cmd));
        if (const auto pointer = niri_pointer_token(key_tok)) {
            if (pointer->kind == NiriPointerKind::Touchpad)
                warn_bind(ctx.path, ctx.lineno, "binds: touchpad scroll binds mapped to wheel buttons on X11");
            append_niri_pointer_bind(ctx, modmask, pointer->button, flags, fn, std::move(command));
            return;
        }
        uint8_t kc = 0;
        KeySym  ks = static_cast<KeySym>(0);
        if (!keysym_parse_bind(wm::config::expand::expand_all(key_tok, ctx.conf_vars), &kc, &ks)) {
            warn_bind(ctx.path, ctx.lineno, "binds: unknown key '" + key_tok + "'; skipping");
            return;
        }
        ctx.keys.push_back(Key{modmask, ks, kc, flags, cooldown_ms, 0U, fn, std::move(command)});
    }

    /* Parse niri-style `binds { ... }` block entries. */
    void parse_binds_block(BindingsContext ctx, std::istream& in, unsigned int& lineno) {
        int         nest = 1;
        std::string line;
        while (std::getline(in, line)) {
            ++lineno;
            line = strip_line_comment(std::move(line));
            if (line.empty())
                continue;
            if (line == "}") {
                --nest;
                if (nest == 0)
                    return;
                if (nest < 0) {
                    warn_bind(ctx.path, lineno, "binds: unexpected '}'");
                    return;
                }
                continue;
            }
            if (trim(line) == "binds {") {
                ++nest;
                continue;
            }
            if (nest != 1) {
                warn_bind(ctx.path, lineno, "binds: nested blocks are not supported; skipping line");
                continue;
            }
            parse_niri_bind_entry(ctx, line);
        }
        warn_bind(ctx.path, lineno, "binds: unterminated binds { block");
    }

    /* Parse layout line, resolve layout function, and append layout entry. */
    void parse_layout_line(BindingsContext ctx, std::string_view value) {
        /* layout = name symbol   symbol may be [T] or T */
        std::istringstream iss{std::string(value)};
        std::string        name;
        std::string        sym;

        iss >> name >> sym;
        if (name.empty())
            common::throw_parse_error(ctx.path, ctx.lineno, "layout needs name and symbol");
        if (sym.empty())
            sym = name;
        if (sym.size() >= 2 && sym.front() == '[' && sym.back() == ']')
            sym = sym.substr(1, sym.size() - 2);

        LayoutFn fn = ctx.layout_by_name(name);
        ctx.layouts.push_back(Layout{sym, fn});
    }

} /* namespace wm::config::parse */
