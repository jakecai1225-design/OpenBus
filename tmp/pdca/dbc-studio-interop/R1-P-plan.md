# R1 Plan — Edit-workspace interop

## Gaps

- Attributes values pane thin (name/value only); defaults not visible
- No search on Value tables / Attributes
- Focus carried, but no jump buttons / context menus between pages
- `set_focus` did not notify other pages live

## Goals

1. Search filters on Value tables + Attributes
2. Attributes values show Value / Default / Type / Source; include orphans + GenMsgCycleTime mirror
3. Context menus + Related buttons: Messages ↔ Attributes ↔ Value tables
4. `document.on_focus` + `goto_attributes` / `goto_value_tables`

## Acceptance

- [ ] Select signal on Messages → Attributes target follows; Value tables prefers its VAL_
- [ ] Right-click tree → Open Attributes / Value table
- [ ] Attributes lists all defs for scope with default/source
- [ ] Search filters both lists
- [ ] Unit tests green
