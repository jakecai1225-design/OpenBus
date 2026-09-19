# Suite UI / UX Design System

> Source of truth: **UDS Suite** (2026-09-20).  
> All domain suites (`*-suite`, `dbc-studio`, `tx-lab`, `bus-*`, `protocol-hub`, `log-analysis`, `ai-agent`) must follow this document.  
> Chinese is allowed in Markdown only. Code / UI strings stay English (`no-chinese-in-code`).  
> Rollout tracker: [Domain_Suite_Rollout_Plan.md](Domain_Suite_Rollout_Plan.md) · Requirements: [Domain_Suite_Competitive_Requirements.md](Domain_Suite_Competitive_Requirements.md)

---

## 1. Product goal

Each suite is a **standalone-grade domain application**, not a dialog. It must feel like VS Code Light: calm, sparse borders, clear hierarchy, muscle-memory controls.

**UDS is the reference implementation.** When in doubt, open `plugins/uds-suite/` and copy the pattern.

---

## 2. Information architecture

| Layer | Rule |
|-------|------|
| Left nav | Primary workspaces only (Setup, Diagnose, …). Fixed width ~148px. Icon + label. |
| Top row of a workspace | Business tabs only when needed (e.g. Services / DID / DTC). **First row**, underline style. |
| Setup / Settings | Connection, session, timing, IDs live **only** on a Setup page — never repeated on every business page. |
| Status | Thin status bar for last traffic / hint. No permanent Bus strip on business pages. |
| OUTPUT | Only on pages that send or collect live traffic. Splitter under main work. Min height ~160px. |
| Log page | Full activity log for the suite. |

Do **not** stack: Bus status + guide caption + bordered cards + tab chrome on the same page.

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

## 4. Shell skeleton (required)

```
QMainWindow
 ├─ SuiteNav (QListWidget)     # icons + titles
 └─ SuiteContent
     └─ QStackedWidget         # one page per nav key
         └─ page()
             ├─ optional QTabBar (top) + QStackedWidget
             ├─ business body
             └─ optional OUTPUT splitter
```

Shared helpers (preferred):

| Helper | Role |
|--------|------|
| `vscode_theme.apply` | Stylesheet |
| `codicons` | Icons |
| `_shared/suite_chrome.py` | Nav build, page margins, status |
| `plugin_shell` | Status bar, CSV export, empty state |
| `state_store` | Persist nav + settings |

---

## 5. Page layout rules

1. Margins: content ~`16,12,16,12`; spacing ~`12–16`.
2. Primary work gets stretch; chrome stays fixed height.
3. One primary action per region (Apply / Send / Start / Run).
4. Guide text → **tab tooltip** or section hint — not a permanent banner under every tab.
5. English UI strings only.

---

## 6. Acceptance checklist (every suite)

- [ ] `vscode_theme.apply(self)` on shell
- [ ] Left nav with codicon icons
- [ ] No Bus / Session strip on business pages (Setup only, if needed)
- [ ] Flat sections (no card soup)
- [ ] StepSpin for numeric IDs / timeouts
- [ ] OUTPUT only where live TX/RX matters
- [ ] `state_store` restores last nav page
- [ ] Ctrl+1…N switches nav pages
- [ ] Can be understood without reading a wall of captions

---

## 7. Non-goals

- Dark theme (unless product later adds a toggle)
- Chinese in `.py` / UI chrome
- Reintroducing thin single-purpose market plugins
- Copying CANoe density; we copy clarity, not clutter
