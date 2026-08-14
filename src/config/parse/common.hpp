/* Shared parse error and expected-unwrapping helpers. */
#pragma once

#include "config/parse/utils.hpp"

#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace wm::config::parse::common {

    /* Resolve printable source path fallback for parse diagnostics. */
    [[nodiscard]] inline std::string_view source_or_unknown(const char* path) noexcept {
        return (path && *path) ? std::string_view(path) : std::string_view("?");
    }

    /* Build shared parse-diagnostic prefix (`zestwm: <source>`). */
    [[nodiscard]] inline std::string make_context_prefix(const char* path) {
        const std::string_view source = source_or_unknown(path);
        std::string            prefix;
        prefix.reserve(9U + source.size());
        prefix += "zestwm: ";
        prefix.append(source);
        return prefix;
    }

    /* Build contextual parse error string with source line metadata. */
    [[nodiscard]] inline std::string make_context_error(const char* path, unsigned int lineno, std::string_view detail) {
        std::string msg = make_context_prefix(path);
        msg.reserve(msg.size() + 7U + detail.size());
        msg += ":";
        msg += std::to_string(lineno);
        msg += ": ";
        msg += detail;
        return msg;
    }

    /* Build contextual parse error string without explicit line metadata. */
    [[nodiscard]] inline std::string make_context_error(const char* path, std::string_view detail) {
        std::string msg = make_context_prefix(path);
        msg.reserve(msg.size() + 2U + detail.size());
        msg += ": ";
        msg += detail;
        return msg;
    }

    /* Throw parse error with source line context. */
    [[noreturn]] inline void throw_parse_error(const char* path, unsigned int lineno, std::string_view detail) {
        throw std::runtime_error(make_context_error(path, lineno, detail));
    }

    /* Throw parse error with source-only context. */
    [[noreturn]] inline void throw_parse_error(const char* path, std::string_view detail) {
        throw std::runtime_error(make_context_error(path, detail));
    }

    /* Parse one `key = value` line and return trimmed key/value pair.
     *
     * - `context_label` is optional and used only to enrich error wording.
     * - Throws parse error when '=' is missing or key is empty.
     */
    [[nodiscard]] inline std::pair<std::string, std::string> parse_key_value_or_throw(std::string_view line, const char* path, unsigned int lineno,
                                                                                      std::string_view context_label = {}) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) [[unlikely]] {
            std::string detail = "expected key = value";
            if (!context_label.empty()) {
                detail += " inside ";
                detail.append(context_label);
            }
            throw_parse_error(path, lineno, detail);
        }

        std::string key = wm::config::parse::trim(line.substr(0, eq));
        std::string val = wm::config::parse::trim(line.substr(eq + 1));
        if (key.empty()) [[unlikely]] {
            std::string detail = "key cannot be empty";
            if (!context_label.empty()) {
                detail += " inside ";
                detail.append(context_label);
            }
            throw_parse_error(path, lineno, detail);
        }
        return {std::move(key), std::move(val)};
    }

    /* Unwrap expected parse result or throw parse error with source context. */
    template <typename T>
    [[nodiscard]] inline T expect_or_throw(std::expected<T, std::string>&& result, const char* path, unsigned int lineno) {
        if (!result)
            throw_parse_error(path, lineno, result.error());
        return std::move(*result);
    }

} /* namespace wm::config::parse::common */
