/* Runtime storage + evaluation/apply implementation for parsed `window-rule` entries.
 * This file is intentionally side-effect focused (adopt_client-time behavior), while parser/token logic
 * remains in `window_rule_parse.cpp`. */
#include "config/parse/window_rule.hpp"

#include "monitor_select.hpp"
#include "types.hpp"
#include "wm_state.hpp"

#include <cmath>
#include <regex>

namespace wm::config::parse {

    std::vector<WindowRuleEntry> window_rules;

    namespace {
        /* Guard regex_search against null C-string inputs from X property probes.
         * Keeps matching helpers total (no UB from null pointer C-string APIs). */
        [[nodiscard]] const char* cstr_or_empty(const char* s) noexcept {
            return s ? s : "";
        }

        /* Pending effect encoding used by `Client` runtime fields. */
        enum class PendingEffect : unsigned char {
            Unset        = 0U,
            ApplyOnce    = 1U,
            ApplyPersist = 2U,
        };
        [[nodiscard]] bool match_directive_matches(const WindowRuleMatcherDirective& m, const Client* c, const char* class_name) {
            if (m.app_id && !std::regex_search(cstr_or_empty(class_name), *m.app_id))
                return false;
            if (m.title && !std::regex_search(c ? c->name : std::string{}, *m.title))
                return false;
            if (m.is_active.has_value()) {
                const bool active = c && c->mon && c->mon->sel == c;
                if (active != *m.is_active)
                    return false;
            }
            if (m.is_focused.has_value()) {
                const bool focused = c && c->mon && c->mon->sel == c;
                if (focused != *m.is_focused)
                    return false;
            }
            if (m.is_floating.has_value()) {
                const bool floating = c && c->isfloating != 0;
                if (floating != *m.is_floating)
                    return false;
            }
            if (m.is_urgent.has_value()) {
                const bool urgent = c && c->isurgent != 0;
                if (urgent != *m.is_urgent)
                    return false;
            }
            return true;
        }
    } // namespace

    void clear_window_rules() {
        window_rules.clear();
    }

    [[nodiscard]] bool window_rule_entry_matches(const WindowRuleEntry& e, const Client* c, const char* class_name) {
        bool any_match = e.matches.empty();
        for (const auto& m : e.matches) {
            if (match_directive_matches(m, c, class_name)) {
                any_match = true;
                break;
            }
        }
        if (!any_match)
            return false;

        for (const auto& ex : e.excludes) {
            if (match_directive_matches(ex, c, class_name))
                return false;
        }
        return true;
    }

    void window_rule_apply_prestack(const WindowRuleEntry& e, Client* c) {
        if (e.effect_open_floating.has_value())
            c->isfloating = *e.effect_open_floating ? 1 : 0;
        if (e.effect_open_focused.has_value())
            c->neverfocus = *e.effect_open_focused ? 0 : 1;
        if (e.effect_open_maximized.has_value() && *e.effect_open_maximized)
            c->isfloating = 0;
        const Monitor* m            = c->mon;
        const int      wx           = m ? m->wx : 0;
        const int      wy           = m ? m->wy : 0;
        const int      ww           = m ? m->ww : 1;
        const int      wh           = m ? m->wh : 1;
        auto           resolve_size = [](const WindowRuleAxisSpan& s, int area_px) -> int {
            const double raw = s.percent ? (static_cast<double>(area_px) * (s.value / 100.0)) : s.value;
            return std::max(1, static_cast<int>(std::lround(raw)));
        };
        auto resolve_pos = [](const WindowRuleAxisSpan& s, int origin, int area_px) -> int {
            const double raw = s.percent ? (origin + static_cast<double>(area_px) * (s.value / 100.0)) : s.value;
            return static_cast<int>(std::lround(raw));
        };
        if (e.effect_open_size.has_value()) {
            c->w = resolve_size(e.effect_open_size->first, ww);
            c->h = resolve_size(e.effect_open_size->second, wh);
        }
        if (e.effect_open_move.has_value()) {
            c->x = resolve_pos(e.effect_open_move->first, wx, ww);
            c->y = resolve_pos(e.effect_open_move->second, wy, wh);
        }
        if (e.effect_open_center.has_value() && *e.effect_open_center)
            c->rule_center_pending = static_cast<unsigned char>(PendingEffect::ApplyPersist);
        if (e.effect_open_fullscreen.has_value())
            c->rule_fullscreen_pending = *e.effect_open_fullscreen ? static_cast<unsigned char>(PendingEffect::ApplyPersist) : static_cast<unsigned char>(PendingEffect::ApplyOnce);

        if (e.effect_open_on_output.has_value() && !e.effect_open_on_output->empty()) {
            if (Monitor* m = monitor_select_resolve(*e.effect_open_on_output))
                c->mon = m;
        }
        if (e.effect_workspace_target.has_value()) {
            WorkspaceRef target_workspace = workspace_normalize_special_ref_with_hidden_id(*e.effect_workspace_target);
            c->workspace                  = std::move(target_workspace);
            c->workspace_set_by_rule      = 1;
            c->workspace_rule_silent      = e.effect_workspace_silent ? 1 : 0;
            /* Normal `workspace N silent` only sets workspace_rule_silent (suppress auto-view on map/activate).
             * Setting workspace_special_silent here too made adopt_client() skip post-map focus and blocked maybe_select_new_client
             * path for tiled clients routed to silent normal workspaces. Reserve workspace_special_silent for overlay routes. */
            c->workspace_special_silent = e.effect_workspace_silent && c->workspace.is_special() ? 1 : 0;
        }
    }

} /* namespace wm::config::parse */
