# Group and groupbar config

Configure grouped (tabbed) clients and the groupbar UI in `zestwm.conf`.

```conf
group {
    focus_removed_window = history
    insert_after_current = true
    auto_group = true
    drag_into_group = 1
    drag_out_of_group = true
    merge_groups_on_drag = true
    col.border_active = #eeeeee
    col.border_inactive = #444444

    groupbar {
        enabled = true
        render_titles = true
        position = top
        col.active = #eeeeee
        col.inactive = #bbbbbb
        col.background = #222222
    }
}
```

## `group.focus_removed_window`

Chooses which remaining tab gets focus when the **focused** grouped client is closed (`killclient` / client exit).

| Value | Behavior |
|-------|----------|
| `history` | Prefer last previously active tab (`prev_active`), else previous tab, else next. **Default.** |
| `previous` / `prev` | Previous visible tab, else next. |
| `next` | Next visible tab, else previous. |
| `first` | First remaining visible tab. |
| `last` | Last remaining visible tab. |
| `leave` / `none` | No in-group fallback; normal global focus path. |

Aliases: `prev` → `previous`; `none` → `leave`.

Moving a tab out of a group (`moveoutofgroup`) keeps focus on the moved client; this key does not change that path.

## `group.drag_out_of_group`

When `true` (**default**), releasing a `movemouse` drag of a multi-tab grouped client onto empty space (not another tiled client, not a groupbar) peels that client into a tiled split sibling. Split side follows the drag delta (`l`/`r`/`u`/`d`). Merge-on-drop still wins when a merge target is present. Never toggles floating.

## Other `group` keys (summary)

- `auto_group` - auto-attach into grouped leaves when layout policy allows.
- `insert_after_current` - insert new clients after the active tab.
- `drag_into_group` - mouse-drag merge into an existing group (0/1/2 modes).
- `drag_out_of_group` - empty-drop peel out of a multi-tab group (see above).
- `merge_groups_on_drag` - drag of a grouped client can merge whole groups.
- `col.border_active` / `col.border_inactive` - border colors for grouped clients.

## `groupbar { }` (summary)

- `enabled` / `render_titles` - show bar and titles.
- `position` - `top`/`left`/`right`/`bottom` (or `t`/`l`/`r`/`b`).
- `col.active` / `col.inactive` / `col.background` - tab and bar colors.
- `indicator_height` / `indicator_gap` - active indicator geometry.
- `font_family` / `font_size` - optional bar font override.

Left-click on a groupbar tab focuses that visible client (built-in; no bind required).
