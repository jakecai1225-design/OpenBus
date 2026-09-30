# R2-D · Do — DBC Studio norm (local)

**Role:** Engineering  
**Date:** 2026-09-29

## Changes

- Caption hint rows removed from compare/merge/library/timing/matrix/export/attributes/value_tables
- Pages use `pages/_ui` tokens (`CTRL_H`) and `tool_strip` / `ghost_btn` / `primary_btn` on Validate, Compare, Merge, Library, Timing, Matrix
- Editor field heights use `_ui.CTRL_H`
- `test_shell_routes.test_no_caption_hints`

## Verify

All dbc-studio tests PASS.
