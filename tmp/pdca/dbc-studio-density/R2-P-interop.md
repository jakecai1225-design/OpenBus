# DBC Value tables / Attributes interop

## Root cause

Most Vector DBCs only contain inline `VAL_ <id> <sig> …` without `VAL_TABLE_`.
Parser stored encodings on `signal.value_table` (Messages inspector OK) but left
`db.value_tables` empty — so the Value tables leaf showed nothing.

Attributes already had defs/values, but did not follow Messages selection
(`document.focus_*` was never set from the editor tree).

## Fix

1. `dbcparse.DbcFile.sync_value_tables_from_signals()` after parse (+ serialize)
2. Value tables: Used-by column, refresh on leaf activate, auto-select focused signal’s table
3. Editor: `document.set_focus` on tree select
4. Attributes: sync Message/Signal target from focus on refresh / leaf activate
5. AppShell `_activate_feature` calls `page.refresh()` when present

## Verify

Reopen the DBC → Edit → Value tables should list `VT_<Msg>_<Sig>` (or named tables).
Select a signal in Messages, then open Value tables / Attributes — selection follows.
