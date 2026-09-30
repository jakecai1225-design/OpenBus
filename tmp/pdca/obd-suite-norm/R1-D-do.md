# R1-D · Do — OBD Suite norm (shell)

**Role:** Engineering  
**Date:** 2026-09-30

## Changes

- Rewrote `app_shell.py` on `suite_chrome`; activities Scanner / Readiness / Setup; Log removed from nav
- `session.py`: `apply_ids`, `set_focus`, `next_hint`, `advance_next_hint`
- New `pages/_ui.py` with TOOL_H=36 density + strip field borders
- Setup / Readiness / Scanner use `tool_strip` / `inline_filter`; connection IDs only on Setup
- `tests/test_shell_routes.py`

## Why

Wave 2 first target per Domain_Suite_Norm_Remap_Plan — OBD lacked suite_chrome and used Log as activity.
