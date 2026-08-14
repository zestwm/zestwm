#pragma once

/* Logical workspace identity: unset, numeric desktop (positive WorkspaceId), or special scratchpad (`special:<tag>`). */
/* Used to extend client/monitor state beyond `WorkspaceId` alone; EWMH mapping for specials is intentionally separate. */

#include "workspace_id.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

enum class WorkspaceKind : std::uint8_t {
    Unset   = 0,
    Normal  = 1,
    Special = 2,
};

struct WorkspaceRef {
    WorkspaceKind                               kind{WorkspaceKind::Unset};
    WorkspaceId                                 normal_id{0};
    std::string                                 special_tag{};

    [[nodiscard]] static constexpr WorkspaceRef unset() noexcept {
        return {};
    }

    [[nodiscard]] static WorkspaceRef normal(WorkspaceId id) noexcept {
        WorkspaceRef r{};
        r.kind      = WorkspaceKind::Normal;
        r.normal_id = id;
        return r;
    }

    [[nodiscard]] static WorkspaceRef special(std::string tag) noexcept {
        WorkspaceRef r{};
        r.kind        = WorkspaceKind::Special;
        r.special_tag = std::move(tag);
        return r;
    }

    [[nodiscard]] constexpr bool is_unset() const noexcept {
        return kind == WorkspaceKind::Unset;
    }
    [[nodiscard]] constexpr bool is_normal() const noexcept {
        return kind == WorkspaceKind::Normal;
    }
    [[nodiscard]] constexpr bool is_special() const noexcept {
        return kind == WorkspaceKind::Special;
    }
};

[[nodiscard]] inline bool operator==(const WorkspaceRef& a, const WorkspaceRef& b) noexcept {
    if (a.kind != b.kind)
        return false;
    if (a.kind == WorkspaceKind::Unset)
        return true;
    if (a.kind == WorkspaceKind::Normal)
        return a.normal_id == b.normal_id;
    return a.special_tag == b.special_tag;
}

struct WorkspaceRefHash {
    [[nodiscard]] std::size_t operator()(const WorkspaceRef& r) const noexcept {
        const auto  kh = std::to_underlying(r.kind);
        std::size_t h  = std::hash<std::underlying_type_t<WorkspaceKind>>{}(kh);
        if (r.kind == WorkspaceKind::Normal) {
            h ^= std::hash<WorkspaceId>{}(r.normal_id) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        } else if (r.kind == WorkspaceKind::Special) {
            h ^= std::hash<std::string>{}(r.special_tag) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return h;
    }
};

namespace wm::workspace_ref {

    [[nodiscard]] inline std::string_view trim_token(std::string_view s) noexcept {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
            s.remove_suffix(1);
        return s;
    }

    /* ASCII case-insensitive equality for short fixed literals (prefix checks). */
    [[nodiscard]] inline bool ascii_ieq_8(std::string_view a, const char (&lit)[9]) noexcept {
        if (a.size() < 8U)
            return false;
        for (std::size_t i = 0; i < 8U; ++i) {
            const unsigned char ca = static_cast<unsigned char>(a[i]);
            const unsigned char cb = static_cast<unsigned char>(lit[i]);
            if (std::tolower(ca) != std::tolower(cb))
                return false;
        }
        return true;
    }

    /* Parse `special:<tag>` (tag may be empty) or a decimal workspace id (positive WorkspaceId). Named normal workspaces are not resolved here. */
    [[nodiscard]] inline std::expected<WorkspaceRef, std::string> parse_workspace_ref_token(std::string_view token) {
        const std::string_view t = trim_token(token);
        if (t.empty())
            return std::unexpected("workspace token is empty");
        if (t.size() >= 8U && ascii_ieq_8(t, "special:")) {
            return WorkspaceRef::special(std::string(t.substr(8U)));
        }
        const std::expected<WorkspaceId, std::string> id = parse_workspace_id(t);
        if (!id)
            return std::unexpected(id.error());
        return WorkspaceRef::normal(*id);
    }

} /* namespace wm::workspace_ref */

/* Bind/IPC payload: normal workspace id or `special:<tag>` (see `parse_workspace_dispatch_token` in action.cpp). */
struct WorkspaceArgPayload {
    WorkspaceRef ref;
    bool         silent = false;
    /* Hidden workspace id bridge for `special:<tag>` (phase-4 migration metadata). */
    std::optional<WorkspaceId> hidden_id = std::nullopt;
};
