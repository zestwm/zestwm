#include "zestctl/helpers.hpp"

#include "config/parse/utils.hpp"

#include <cstdio>
#include <cstdlib>

/* Print zestctl command usage and supported subcommands. */
void usage(void) {
    fprintf(stderr,
            "usage:\n"
            "  zestctl dispatch workspace <index|name|next|prev>\n"
            "  zestctl dispatch movetoworkspace <index|name|next|prev> [window-id]\n"
            "  zestctl dispatch focusmonitor <+1|-1>\n"
            "  zestctl dispatch killclient|focusurgent|focuswindow <window-id>\n"
            "  zestctl dispatch togglefloating|togglefullscreen|reload|quit|layout <name|index|next|prev>\n"
            "  zestctl dispatch layoutmsg splitratio <value> [exact] | swapsplit | togglesplit | preselect <l|r|u|d|t|b> | movetoroot [active] [unstable]\n"
            "  zestctl version|monitors|layouts [--all]|workspaces|activeworkspace|activewindow|clients\n"
            "  zestctl reload|quit\n"
            "  zestctl [-j|--json] <command>\n"
            "  zestctl --batch \"<cmd1> ; <cmd2> ; ...\"\n");
}

/* Tokenize a command line by spaces/tabs/newlines (no quote handling here). */
std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t                   i = 0;

    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
            i++;
        if (i >= s.size())
            break;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\n')
            j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

/* Tokenize `--batch` input by `;`, then trim each command chunk. */
std::vector<std::string> split_batch(const std::string& s) {
    std::vector<std::string> out;
    size_t                   i = 0;

    while (i <= s.size()) {
        size_t p = s.find(';', i);
        if (p == std::string::npos)
            p = s.size();
        std::string part = wm::config::parse::trim(s.substr(i, p - i));
        if (!part.empty())
            out.push_back(part);
        if (p == s.size())
            break;
        i = p + 1;
    }
    return out;
}

/* Escape control characters and quotes for JSON output mode. */
std::string json_escape(const std::string& s) {
    std::string out;
    size_t      i;

    out.reserve(s.size() + 8);
    for (i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

/* Convert float payload to signed 32-bit fixed-point (x10000). */
uint32_t encode_fixed4(double v) {
    const double scaled = v * 10000.0;
    const double biased = scaled >= 0.0 ? (scaled + 0.5) : (scaled - 0.5);
    return static_cast<uint32_t>(static_cast<int32_t>(biased));
}

/* Parse X11 window id from decimal/hex CLI token with range checks. */
std::expected<uint32_t, std::string> parse_window_id_token(const std::string& token) {
    char*               end    = nullptr;
    const unsigned long parsed = strtoul(token.c_str(), &end, 0);
    if (token.empty() || end == token.c_str() || (end && *end != '\0'))
        return std::unexpected("invalid window id token");
    if (parsed == 0UL || parsed > 0xffffffffUL)
        return std::unexpected("window id out of range");
    return static_cast<uint32_t>(parsed);
}
