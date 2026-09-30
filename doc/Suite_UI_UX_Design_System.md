# Suite UI / UX Design System

> Source of truth: **VS Code Light four-piece chrome** (see [插件开发规范.md](插件开发规范.md) §3).  
> Reference shells: `uds-suite` · `obd-suite` · `j1939-suite` · `canopen-suite` · `dbc-studio` · `eds-studio`.  
> All domain suites (`*-suite`, `*-studio`) must follow this document.  
> Retired mixed hubs under `plugins/_retired/` are out of product scope.  
> Chinese is allowed in Markdown only. Code / UI strings stay English (`no-chinese-in-code`).  
> Norm remap: [Domain_Suite_Norm_Remap_Plan.md](Domain_Suite_Norm_Remap_Plan.md)

---

## 1. Product goal

Each suite is a **standalone-grade domain application**, not a dialog. It must feel like VS Code Light: Activity · Side Bar · Editor Tabs · OUTPUT, calm borders, clear hierarchy.

When in doubt, open a reference shell above and copy the pattern — especially `suite_chrome` + `suite_tabs` + `suite_ui`.

---

## 2. Information architecture

| Layer | Rule |
|-------|------|
| Activity bar | Major workspaces only (≤5). Codicon + label. **Log is not an Activity.** |
| Side Bar | Leaves for the current Activity (`workspace_sidebar`). Ctrl+B; default visible. One leaf = one job. |
| Editor Tabs | Closable tabs via `suite_tabs`; leaf click opens/focuses a tab. Single-leaf Activity still gets a tab. |
| Setup | Connection / session / timing **only** on Setup (or thin status) — never a full-width strip on every page. |
| Status | Thin status bar: path / dirty / session / Context Next. |
| OUTPUT | **Shell panel only** (one tool row + list). Ctrl+J; default visible. Not per-page, not an Activity. |

Do **not** stack: Bus strip + guide caption + bordered cards + a second nav tab bar on the same page.

---

## 3. Visual system

### 3.1 Theme

- Module: `plugins/_shared/vscode_theme.py`
- Apply once on the suite window: `vscode_theme.apply(window)`
- Palette: VS Code Light (`#F3F3F3` / `#FFFFFF` / accent `#007ACC`)
- Type scale: **13px body**, **12px hint**, **11px meta**, Consolas **12px** for hex/PDU

### 3.2 Borders (anti–card-soup)

Industry pattern (VS Code / Linear): **one continuous surface**.

| Allowed | Forbidden |
|---------|-----------|
| Nav right edge | Nested bordered cards for every section |
| Tab underline (selected) | Boxed tab chrome |
| Splitter hairline | Gray header bars inside every block |
| Control borders (inputs, buttons) | Double frames around tables + panels |
| Optional bottom hairline under a status row | Dark “rounded box” walls everywhere |

Sections use **title + hint + whitespace** (`vscode_theme.block()`), not boxes.

### 3.3 Icons

- Module: `plugins/_shared/codicons.py` + `plugins/_shared/icons/*.svg`
- VS Code–style monochrome SVG, tinted with `currentColor`
- Nav items: `codicons.set_nav_item(item, key)`
- Buttons: `codicons.set_button(btn, "apply", primary=True)` — no character glyphs (`…`, `·`, emoji)
- Spinners: `widgets/step_spin.StepSpin` (28×28 chevron buttons). Never native tiny spin arrows.

### 3.4 Controls

- Buttons: height **28px**, Primary = filled accent, Secondary/Ghost = bordered white
- Every clickable control must look operable (visible border on buttons)
- Form labels right-aligned via `vscode_theme.tune_form(form)`

---

## 4. Shell skeleton (required) — VS Code four-piece

Every domain suite (`*-suite` / `*-studio`) **must** ship the same workbench as VS Code Light. No half-shells.

```
QMainWindow
 ├─ Menu bar              # File · Edit · View · Help; layout toggles on right corner
 ├─ Activity bar          # NAV_PAGES, ≤5, codicons
 ├─ Side Bar              # workspace leaves; Ctrl+B; default visible
 ├─ Editor chrome row     # closable tabs (suite_tabs) OR page title only — no layout icons
 │    └─ page content     # tool_strip → work surface
 ├─ OUTPUT panel          # one tool row + list; Ctrl+J; default visible
 └─ Status bar            # path / dirty / session / Context Next
```

| VS Code | OpenBus | Required |
|---------|---------|----------|
| Activity Bar | Activity strip | Yes |
| Side Bar | `side_bar_enabled=True` + `workspace_sidebar` | Yes |
| Editor Tabs | `_open_tabs` + `suite_tabs.mount_editor_tabs` | Yes |
| Panel | OUTPUT (`panel_visible=True`) | Yes |
| Status Bar | status + Context Next | Yes |
| Menu | File · Edit · View · Help | Yes |
| Layout toggles | Menubar trailing (`attach_layout_toggles_to_menubar`), not editor chrome | Yes |

**Must implement:** `_on_workbench_page` so `highlight_activity` never wipes tabs via `set_editor_title`.  
**Must not:** Log as an Activity; per-page OUTPUT copies; sidebar-only stack swap without tabs; layout icons on the editor tab row (put them on the menubar right corner like VS Code).

Shared helpers:

| Helper | Role |
|--------|------|
| `vscode_theme.apply` | Stylesheet |
| `codicons` | Icons |
| `_shared/suite_chrome.py` | Activity + Side Bar + OUTPUT host; `attach_layout_toggles_to_menubar` |
| `_shared/suite_tabs.py` | Closable editor tab strip |
| `_shared/suite_ui.py` | Density tokens + tool_strip / polish |
| `plugin_shell` | Status bar, CSV export |
| `state_store` | Persist nav + **open_tabs** + layout |

Canonical detail: [插件开发规范.md](插件开发规范.md) §3 / §13.1 / §13.8.

---

## 5. Page layout rules

1. Page chrome: `tool_strip` / `panel_header` (`TOOL_H=36`) → optional `inline_filter` → stretch work surface.
2. Primary work gets stretch; chrome stays fixed height.
3. One primary action per region (Apply / Send / Start / Run).
4. Guide text → **tooltip** — not a permanent caption banner.
5. English UI strings only.
6. Prefer `suite_ui` helpers (`ghost_btn` / `primary_btn` / `style_tree` / `polish_work_surface`).

---

## 6. Acceptance checklist (every suite)

- [ ] VS Code four-piece: Activity + Side Bar + Editor Tabs + OUTPUT (§3)
- [ ] `side_bar_enabled=True`; `_open_feature_tab` / `_on_workbench_page` / `suite_tabs`
- [ ] `vscode_theme` + `suite_ui` (or re-export); TOOL_H = CTRL_H + 2×STRIP_PAD_V
- [ ] Left Activity with codicon icons; ≤5; no Log Activity
- [ ] No Bus / Session strip on business pages (Setup only, if needed)
- [ ] Flat sections (no card soup)
- [ ] StepSpin for numeric IDs / timeouts where applicable
- [ ] OUTPUT default visible; Ctrl+B / Ctrl+J
- [ ] `state_store` restores nav + open_tabs
- [ ] Ctrl+1…N switches Activity
- [ ] `tests/test_shell_routes.py` asserts four-piece symbols
- [ ] Can be understood without reading a wall of captions

---

## 7. Non-goals

- Dark theme (unless product later adds a toggle)
- Chinese in `.py` / UI chrome
- Reintroducing thin single-purpose market plugins
- Copying CANoe density; we copy clarity, not clutter
