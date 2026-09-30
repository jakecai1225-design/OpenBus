# R1 Plan — DBC editor tab residency

## Gap

DBC Studio used `set_editor_title` + stack swap only. Sidebar leaves did not stay as closable main-window tabs (规范: editor tabs when workspace has sub-features).

## Goals

1. Sidebar / View / `goto_page` open or focus a closable `SuiteEditorTabs` tab.
2. Tabs persist across activity switches; close last tab reopens Messages.
3. Persist/restore `open_tabs` in suite state.
4. Document path / Save stay on status bar (not a second chrome row).

## Acceptance

- [ ] Leaf click adds tab; re-click focuses existing tab
- [ ] Tab close removes leaf; empty → Messages
- [ ] Chrome row is tab strip when tabs open (no title-only replacement as sole mode)
- [ ] Unit tests cover normalize + symbols; no Chinese in code

## Out of scope

- Per-document multi-DBC tabs
- Custom tab painter / drag between suites
