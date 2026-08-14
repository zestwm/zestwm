#pragma once

#include <cstdint>

#include <expected>
#include <string>
#include <vector>

/* Shared CLI/text helpers for zestctl parsing and output formatting. */
void usage(void);

/* Split a command string by ASCII whitespace. */
[[nodiscard]] std::vector<std::string> split_ws(const std::string& s);
/* Split `--batch` payload on `;` and trim each command. */
[[nodiscard]] std::vector<std::string> split_batch(const std::string& s);
/* Escape a UTF-8 string for JSON string literal emission. */
[[nodiscard]] std::string json_escape(const std::string& s);
/* Encode floating payload using signed fixed-point 1/10000 precision. */
[[nodiscard]] uint32_t encode_fixed4(double v);
/* Parse decimal or hex (`0x...`) X11 window token. */
[[nodiscard]] std::expected<uint32_t, std::string> parse_window_id_token(const std::string& token);
