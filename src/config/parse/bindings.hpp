/* Config `binds {}` parser helper declarations.
 *
 * Role:
 * - Parse niri-style `binds {}` plus layout config lines into runtime structures.
 *
 * Scope:
 * - Parsing and validation only; does not execute dispatcher callbacks.
 * - Emits warnings/errors through parser diagnostics helpers.
 */
#pragma once

#include "config.hpp"
#include "types.hpp"

#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace wm::config::parse {

    using KeyFn          = void (*)(const wm::config::parse::ActionCommand*);
    using LayoutFn       = void (*)(Monitor*);
    using LayoutResolver = LayoutFn (*)(std::string_view);

    /* Mutable parsing state used by bind/rule/layout line parsers. */
    struct BindingsContext {
        std::vector<Key>&    keys;
        std::vector<Button>& buttons;
        std::vector<Layout>& layouts;
        const ConfVars&      conf_vars;
        const char*          path;
        unsigned             lineno;
        LayoutResolver       layout_by_name;
    };

    /* Parse modifier token string into X11 bitmask.
     *
     * Returns false and reports first unknown token through `bad_token_out`.
     */
    [[nodiscard]] bool parse_mods_checked(std::string_view s, unsigned int* mask_out, std::string* bad_token_out);

    /* Parse key field token into either keycode or keysym representation. */
    [[nodiscard]] bool keysym_parse_bind(std::string_view raw, uint8_t* keycode_out, KeySym* keysym_out);

    /* Resolve dispatcher function pointer from config action name. */
    [[nodiscard]] KeyFn func_by_name_try(std::string_view name);

    /* Parse niri-style `binds { ... }` block and append key bindings. */
    void parse_binds_block(BindingsContext ctx, std::istream& in, unsigned int& lineno);

    /* Parse layout line and append one layout entry. */
    void parse_layout_line(BindingsContext ctx, std::string_view value);

} /* namespace wm::config::parse */
