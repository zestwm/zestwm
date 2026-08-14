/* Pick next focus client when a tab leaves a grouped leaf (respects group.focus_removed_window). */
#pragma once

#include "types.hpp"

/* `removing` must still be linked in its grouped leaf. Returns nullptr when policy is leave or no candidate. */
[[nodiscard]] Client* group_focus_client_after_remove(Client* removing) noexcept;
