# R1 Do

## Changes

- `plugins/dbc-studio/app_shell.py`
  - `normalize_open_tabs`, `_open_tabs` / `_tab_bar` / `_tab_guard`
  - `_mount_tab_bar`, `_open_feature_tab`, `_close_feature_tab`, `_tab_title`
  - `goto_page` → open tab; activity click opens default only when no tabs
  - Persist/restore `open_tabs`
  - Removed title-only `_mount_chrome` / `set_editor_title` path
- `plugins/dbc-studio/tests/test_shell_routes.py` — tab symbols + normalize tests

## Why

Align with CANopen and 插件开发规范 §3 editor-tab contract.
