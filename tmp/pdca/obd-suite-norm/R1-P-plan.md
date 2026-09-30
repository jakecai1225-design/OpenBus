# R1-P · Plan — OBD Suite norm (shell)

**Role:** Design  
**Date:** 2026-09-30

## Goals

1. Migrate to `suite_chrome.build_workbench` + `vscode_theme` / `_ui`.
2. Activities ≤ 3: Scanner / Readiness / Setup. **No Log activity** (OUTPUT panel only).
3. `run_action` + session `next_hint` / Context Next on status bar.
4. Density tokens: `TOOL_H = CTRL_H + 2×STRIP_PAD_V` (36).
5. Connection IDs owned by Setup (session); Scanner consumes session IDs.

## Acceptance

- [ ] NAV_PAGES == scanner, readiness, setup
- [ ] No `"log"` activity key; log page not in stack
- [ ] `suite_chrome.build_workbench` + Ctrl+B / Ctrl+J
- [ ] `run_action` + `_sync_next_hint`
- [ ] `tests/test_shell_routes.py` PASS; no CJK in shell/session/_ui

## Out of scope

Deep PID decode changes; Reports activity; Protocol algorithm work.
