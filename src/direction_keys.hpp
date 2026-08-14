/* Shared directional key helpers for action/BSP policies.
 *
 * Scope:
 * - Centralize char-key decoding for direction-driven behavior.
 * - Keep parser-free, constexpr-friendly helpers usable in hot paths.
 */
#pragma once

#include <optional>

enum class DirectionKey : unsigned char {
    Left   = 'l',
    Right  = 'r',
    Up     = 'u',
    Down   = 'd',
    Top    = 't',
    Bottom = 'b'
};

/* Decode integer/char-like action payload into a direction enum.
 *
 * Input model:
 * - Callers typically pass integer payloads from typed action commands.
 * - Valid values are the directional key chars (`l/r/u/d/t/b`).
 *
 * Return:
 * - `DirectionKey` when payload maps to a supported direction.
 * - `std::nullopt` for unknown values.
 *
 * Notes:
 * - This helper intentionally performs only value decoding.
 * - Policy validation (e.g. "focus accepts only l/r/u/d") belongs to caller.
 */
[[nodiscard]] constexpr std::optional<DirectionKey> direction_key_from_int(int value) noexcept {
    using U        = std::underlying_type_t<DirectionKey>;
    const auto raw = static_cast<U>(value);

    switch (raw) {
        case static_cast<U>(DirectionKey::Left): return DirectionKey::Left;
        case static_cast<U>(DirectionKey::Right): return DirectionKey::Right;
        case static_cast<U>(DirectionKey::Up): return DirectionKey::Up;
        case static_cast<U>(DirectionKey::Down): return DirectionKey::Down;
        case static_cast<U>(DirectionKey::Top): return DirectionKey::Top;
        case static_cast<U>(DirectionKey::Bottom): return DirectionKey::Bottom;
        default: [[unlikely]] return std::nullopt;
    }
}

[[nodiscard]] constexpr inline bool direction_is_vertical_split_hint(DirectionKey key) noexcept {
    return key == DirectionKey::Left || key == DirectionKey::Right;
}

/* Return true when direction maps to horizontal split axis preference.
 *
 * Horizontal family includes:
 * - movement keys: `u`, `d`
 * - edge aliases:  `t`, `b`
 */
[[nodiscard]] constexpr inline bool direction_is_horizontal_split_hint(DirectionKey key) noexcept {
    return key == DirectionKey::Up || key == DirectionKey::Down || key == DirectionKey::Top || key == DirectionKey::Bottom;
}

/* Return preferred split child order from direction:
 * - true: place new node first (`l/u/t`)
 * - false: place new node second (`r/d/b`)
 *
 * API shape:
 * - Returns `std::optional<bool>` to preserve explicit "no preference" extension room.
 * - Current enum values always map to a preference; callers can still treat missing
 *   preference as fallback-to-policy for forward compatibility.
 */
[[nodiscard]] constexpr inline std::optional<bool> direction_prefers_new_first(DirectionKey key) noexcept {
    switch (key) {
        case DirectionKey::Left:
        case DirectionKey::Up:
        case DirectionKey::Top: return true;
        case DirectionKey::Right:
        case DirectionKey::Down:
        case DirectionKey::Bottom: return false;
    }
    return std::nullopt;
}
