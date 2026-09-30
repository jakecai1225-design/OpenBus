# DBC Studio — editor tabs from sidebar

**Round:** 1  
**Phase:** closed (hotfixed tab wipe on leaf click)  
**Updated:** 2026-09-30

## Goal

Sidebar leaf clicks open closable editor tabs on the main chrome row (VS Code / 规范 §3), matching CANopen `_open_feature_tab`.

## State

- R1 P/D/C/A complete
- Hotfix: leaf activate no longer clears tab strip via `set_editor_title`
- Code: `plugins/dbc-studio/app_shell.py`, `plugins/_shared/suite_chrome.py`
- Tests: `tests/test_shell_routes.py`
