# DBC Studio density — Round 1 Plan

## Gaps (from screenshot + audit)

- Sidebar title shouted (`EDIT` uppercase 11px/700 + letter-spacing)
- Edit toolbar bypassed kit: raw Ghost (transparent border) vs solid Primary
- Tree / empty / Layout used ad-hoc colors and 8pt Consolas
- Status labels hardcoded `#90A4AE;font-size:11px` across pages
- Filter row and buttons did not share one CTRL_H baseline

## Goals

VS Code Light scale for the whole suite:

| Role | Size | Control |
|------|------|---------|
| Body / tree / nav | 13px | — |
| Buttons / fields | 12px | CTRL_H 28 |
| Meta / chrome | 11px | SuiteCount |

## Acceptance

1. Side Bar title is sentence-case `Edit` via SuiteSectionTitle
2. Ghost buttons have visible 1px border; Primary/Ghost both 28px
3. Messages toolbar uses `_ui.tool_strip` / `ghost_btn` / `primary_btn` / `icon_tool`
4. Tree + empty + Layout use kit (no plugin_shell empty_state_label)
5. Layout grid ≥ Consolas 10 / cell 24
6. Unit + smoke tests pass
