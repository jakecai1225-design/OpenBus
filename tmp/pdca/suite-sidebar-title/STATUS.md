# PDCA — Suite Side Bar viewlet title (VS Code parity)

## Status
- Round: 1
- Phase: C (tests pass) → ready for manual Accept after OpenBus restart
- Blocker: none

## Goal
Side Bar top title (e.g. CODE) matches VS Code Explorer viewlet title: 11px muted uppercase Segoe, ~35px row, no clip/giant glyphs.

## Done (R1-D)
- `vscode_theme`: `SuiteSideBarTitle` + header min-height 35 (removed wrong 22px cap)
- `suite_ui` / canopen / dbc: `SIDEBAR_TITLE_H=35`, explicit 11px font on title label
- AUTOSAR sidebars use `sidebar_header` (not `SuiteToolbarTitle` @ 28px)
- All suites: `apply_*_chrome` after `build_workbench` so overlay is not wiped
- Spec §13.14 row; tests `test_sidebar_title.py`

## Manual Accept
1. Restart OpenBus / reactivate CANopen
2. Open Code workspace — title "CODE" is small muted uppercase, not clipped
3. Check AUTOSAR / DBC Side Bar titles same density
