/* Dynamic workspace selector parser/matcher used by `workspace = SELECTOR, RULES` lines. */
#include "workspace_selector.hpp"

#include "intern.hpp"
#include "monitor_select.hpp"
#include "state/runtime_authority.hpp"
#include "types.hpp"
#include "workspace_id.hpp"
#include "workspace_ref.hpp"
#include "workspace_registry.hpp"

#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace wm::workspace_selector {

    namespace {

        [[nodiscard]] std::string_view trim_sv(std::string_view s) noexcept {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }

        [[nodiscard]] bool parse_u64(std::string_view s, std::uint64_t* out) noexcept {
            if (s.empty() || !out)
                return false;
            const auto* begin = s.data();
            const auto* end   = begin + s.size();
            const auto  res   = std::from_chars(begin, end, *out);
            return res.ec == std::errc{} && res.ptr == end;
        }

        [[nodiscard]] bool parse_i32(std::string_view s, int* out) noexcept {
            if (s.empty() || !out)
                return false;
            const auto* begin = s.data();
            const auto* end   = begin + s.size();
            const auto  res   = std::from_chars(begin, end, *out);
            return res.ec == std::errc{} && res.ptr == end;
        }

        [[nodiscard]] bool workspace_name_is_default_numeric(WorkspaceId id) noexcept {
            const WorkspaceMeta* meta = workspace_registry_find_by_id(id);
            if (!meta)
                return true;
            return meta->name == std::to_string(static_cast<unsigned>(id));
        }

        /* Count clients on a normal workspace for `w[...]` selectors. */
        [[nodiscard]] int count_workspace_windows(WorkspaceId id, std::optional<bool> tiled_only, bool visible_only) noexcept {
            if (id < kWorkspaceIdMin)
                return 0;
            const WorkspaceRef ws = WorkspaceRef::normal(id);
            int                n  = 0;
            for (Monitor* mon : wm::state::all_monitors()) {
                for (Client* c : mon->clients) {
                    if (c->isdock || !client_tree_member_on_workspace(c, mon, ws))
                        continue;
                    if (tiled_only.has_value()) {
                        if (*tiled_only && c->isfloating)
                            continue;
                        if (!*tiled_only && !c->isfloating)
                            continue;
                    }
                    if (visible_only && !client_is_visible_on_monitor(c, mon))
                        continue;
                    ++n;
                }
            }
            return n;
        }

        [[nodiscard]] bool workspace_has_fullscreen_client(WorkspaceId id) noexcept {
            if (id < kWorkspaceIdMin)
                return false;
            const WorkspaceRef ws = WorkspaceRef::normal(id);
            for (Monitor* mon : wm::state::all_monitors()) {
                for (Client* c : mon->clients) {
                    if (c->isdock || !client_tree_member_on_workspace(c, mon, ws))
                        continue;
                    if (c->isfullscreen)
                        return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool match_range_count(int count, std::uint64_t from, std::uint64_t to) noexcept {
            if (to < from || from < 1U)
                return false;
            const int lo = static_cast<int>(from);
            const int hi = static_cast<int>(to);
            return count >= lo && count <= hi;
        }

        [[nodiscard]] bool match_clause(char kind, std::string_view prop, WorkspaceId id, Monitor* view_monitor) noexcept {
            if (prop.size() < 3U || prop[0] != kind || prop[1] != '[' || prop.back() != ']')
                return false;
            const std::string_view inner = prop.substr(2U, prop.size() - 3U);

            if (kind == 'r') {
                const std::size_t dash = inner.find('-');
                if (dash == std::string_view::npos)
                    return false;
                std::uint64_t from = 0U;
                std::uint64_t to   = 0U;
                if (!parse_u64(inner.substr(0U, dash), &from) || !parse_u64(inner.substr(dash + 1U), &to))
                    return false;
                if (to < from || from < 1U)
                    return false;
                const int lo  = static_cast<int>(from);
                const int hi  = static_cast<int>(to);
                const int wid = static_cast<int>(id);
                return wid >= lo && wid <= hi;
            }

            if (kind == 's') {
                int want = 0;
                if (!parse_i32(inner, &want))
                    return false;
                const bool is_special = false;
                return (want != 0) == is_special;
            }

            if (kind == 'm') {
                if (!view_monitor)
                    return false;
                Monitor* resolved = monitor_select_resolve(inner);
                return resolved != nullptr && resolved == view_monitor;
            }

            if (kind == 'n') {
                const WorkspaceMeta* meta = workspace_registry_find_by_id(id);
                const std::string    name = meta ? meta->name : std::to_string(static_cast<unsigned>(id));
                if (inner.starts_with("s:")) {
                    const std::string_view prefix = inner.substr(2U);
                    return !prefix.empty() && name.size() >= prefix.size() && name.compare(0U, prefix.size(), prefix) == 0;
                }
                if (inner.starts_with("e:")) {
                    const std::string_view suffix = inner.substr(2U);
                    return !suffix.empty() && name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
                }
                int want = 0;
                if (!parse_i32(inner, &want))
                    return false;
                const bool named = !workspace_name_is_default_numeric(id);
                return (want != 0) == named;
            }

            if (kind == 'w') {
                std::optional<bool> tiled_only;
                bool                visible_only = false;
                std::size_t         pos          = 0U;
                while (pos < inner.size()) {
                    const char flag = inner[pos];
                    if (flag == 't') {
                        if (tiled_only.has_value())
                            return false;
                        tiled_only = true;
                        ++pos;
                        continue;
                    }
                    if (flag == 'f') {
                        if (tiled_only.has_value())
                            return false;
                        tiled_only = false;
                        ++pos;
                        continue;
                    }
                    if (flag == 'v') {
                        visible_only = true;
                        ++pos;
                        continue;
                    }
                    if (flag == 'p' || flag == 'g')
                        return false;
                    break;
                }
                const std::string_view count_part = inner.substr(pos);
                const int              count      = count_workspace_windows(id, tiled_only, visible_only);
                const std::size_t      dash       = count_part.find('-');
                if (dash == std::string_view::npos) {
                    std::uint64_t exact = 0U;
                    if (!parse_u64(count_part, &exact))
                        return false;
                    return static_cast<std::uint64_t>(count) == exact;
                }
                std::uint64_t from = 0U;
                std::uint64_t to   = 0U;
                if (!parse_u64(count_part.substr(0U, dash), &from) || !parse_u64(count_part.substr(dash + 1U), &to))
                    return false;
                return match_range_count(count, from, to);
            }

            if (kind == 'f') {
                int fs_state = 0;
                if (!parse_i32(inner, &fs_state))
                    return false;
                const bool has_fs = workspace_has_fullscreen_client(id);
                if (fs_state == -1)
                    return !has_fs;
                if (fs_state == 0 || fs_state == 1 || fs_state == 2)
                    return has_fs;
                return false;
            }

            return false;
        }

    } // namespace

    bool looks_like_workspace_selector(const std::string_view head) noexcept {
        const std::string_view s = trim_sv(head);
        if (s.empty())
            return false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (std::isspace(static_cast<unsigned char>(s[i])) != 0)
                continue;
            const char c = s[i];
            if (c != 'r' && c != 's' && c != 'n' && c != 'm' && c != 'w' && c != 'f')
                return false;
            if (i + 1U >= s.size() || s[i + 1U] != '[')
                return false;
            return true;
        }
        return false;
    }

    bool matches_normal_workspace(const std::string_view selector, const WorkspaceId id, Monitor* view_monitor) noexcept {
        if (id < kWorkspaceIdMin)
            return false;
        const std::string_view s = trim_sv(selector);
        if (s.empty())
            return true;
        if (const auto parsed_id = parse_workspace_id(s)) {
            return *parsed_id == id;
        }
        if (s.size() >= 5U && (s[0] == 'n' || s[0] == 'N') && s[1] == 'a' && s[2] == 'm' && s[3] == 'e' && s[4] == ':') {
            const WorkspaceMeta* meta = workspace_registry_find_by_id(id);
            return meta && meta->name == s.substr(5U);
        }

        for (std::size_t i = 0; i < s.size(); ++i) {
            if (std::isspace(static_cast<unsigned char>(s[i])) != 0)
                continue;
            const char        kind = s[i];
            const std::size_t end  = s.find(']', i);
            if (end == std::string_view::npos)
                return false;
            const std::string_view prop = s.substr(i, end - i + 1U);
            if (!match_clause(kind, prop, id, view_monitor))
                return false;
            i = end;
        }
        return true;
    }

} /* namespace wm::workspace_selector */
