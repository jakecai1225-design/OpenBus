# R1-P · Plan — EDS Studio norm (shell)

**Role:** Design  
**Date:** 2026-09-30

## Goals

1. Activities: Edit / Analyze / Deliver (≤5). Leaves in side bar.
2. Closable editor tabs (`_open_tabs` + `_on_workbench_page`); close-all reopens Dictionary.
3. File menu owns New/Open/Save/Recent; chrome = tabs + path/dirty + Save + Next.
4. `run_action` + Document `next_hint` / `set_focus`.
5. `vscode_theme` + `_ui` density tokens; strip page `chrome_tabs` from chrome row.

## Acceptance

- [ ] NAV_PAGES == edit, analyze, deliver
- [ ] FEATURE_ROUTE covers editor, pdo, validate, timing, compare, export, library
- [ ] `_open_feature_tab` / `_close_feature_tab` / `_on_workbench_page` present
- [ ] File menu + Recent; no file-button wall on chrome
- [ ] Context Next advances
- [ ] `tests/test_shell_routes.py` PASS; existing document/template tests PASS

## Out of scope

Page interop depth (R2); EDS protocol algorithms.
