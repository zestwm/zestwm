/* RandR-backed monitor selection by numeric id or output name (shared by window rules and workspace routing). */
#pragma once

#include <string>
#include <string_view>

struct Monitor;

/* Resolve selector: decimal `Monitor.num`, or `Monitor.output_name` (e.g. `DP-1`). */
[[nodiscard]] Monitor* monitor_select_resolve(std::string_view selector);

/* `Monitor.output_name` for the given `Monitor.num`, or empty when unknown. */
[[nodiscard]] std::string monitor_select_output_name_for_num(int monitor_num);
