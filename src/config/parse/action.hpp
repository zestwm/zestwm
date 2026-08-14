/* Config bind/button action argument parser helpers.
 *
 * Role:
 * - Parse textual bind arguments into typed command payloads expected by action callbacks.
 *
 * Scope:
 * - This layer is parsing-only: it does not execute actions.
 * - Spawn args are owned as `std::vector<std::string>`; workspace/layoutmsg payloads are by value.
 */
#pragma once

#include "layoutmsg.hpp"
#include "types.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace wm::config::parse {

    using KeyFn = void (*)(const ActionCommand*);

    /* Typed payload model: callback + strongly tagged payload value. */
    struct NoArgPayload final {};
    struct IntPayload final {
        int value{};
    };
    struct FloatPayload final {
        float value{};
    };
    struct SpawnArgvPayload final {
        std::vector<std::string> args;
    };
    struct WorkspaceDispatchPayload final {
        WorkspaceArgPayload payload{};
    };
    struct LayoutMsgDispatchPayload final {
        LayoutMsgPayload payload{};
    };
    struct SetLayoutDispatchPayload final {
        const Layout* layout{};
    };

    enum class ActionPayloadKind : std::uint8_t {
        NoArg,
        Int,
        Float,
        SpawnArgv,
        WorkspaceDispatch,
        LayoutMsgDispatch,
        SetLayoutDispatch,
    };

    using ActionPayload = std::variant<NoArgPayload, IntPayload, FloatPayload, SpawnArgvPayload, WorkspaceDispatchPayload, LayoutMsgDispatchPayload, SetLayoutDispatchPayload>;

    struct ActionCommand final {
        KeyFn             fn{};
        ActionPayloadKind kind{ActionPayloadKind::NoArg};
        ActionPayload     payload{NoArgPayload{}};
    };
    using ParseCommandResult = std::expected<ActionCommand, std::string>;

    /* Parse one bind dispatcher argument into a typed command payload. */
    [[nodiscard]] ParseCommandResult parse_action_command(KeyFn fn, const std::string& argstr);
    /* Execute one typed command through callback entrypoint. */
    void execute_action_command(const ActionCommand& cmd);

} /* namespace wm::config::parse */
