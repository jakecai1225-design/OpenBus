# DBC Studio density — Round 1 Do

## Kit (`pages/_ui.py`)

- Tokens: `FS_BODY=13`, `FS_CTRL=12`, `FS_META=11`, `CTRL_H=28`, `FILTER_H=36`
- `DBC_OVERLAY`: unified Ghost border, control heights, SuiteSectionTitle (not shouty SideBarTitle)
- `section_title`: sentence case → SuiteSectionTitle
- Helpers: `count_label`, improved `empty_state` (SuiteEmptyTitle/Hint)

## Pages

- `editor.py`: kit toolbar, `style_tree`, kit empty state, Layout `panel_header`, BitLayout wiring fixed
- `bit_layout.py`: cell 24, Consolas 10, hit/paint margins aligned
- `value_tables.py` / `attributes.py`: tool_strip + ghost/primary; `style_table`
- `matrix` / `library` / `merge` / `timing` / `export`: SuiteCount + kit styles; drop ad-hoc selection blues

## Not changed

- Shared `vscode_theme.py` base (suite overlay owns DBC density)
- Activity bar icons / File menu IA (already norm-closed)
