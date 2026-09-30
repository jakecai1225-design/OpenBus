# Domain Suite Rollout Plan

> Owner track: suite UI/UX unification after UDS Suite chrome freeze (2026-09-20).  
> Standards: [Suite_UI_UX_Design_System.md](Suite_UI_UX_Design_System.md)  
> Feature PRD: [Domain_Suite_Competitive_Requirements.md](Domain_Suite_Competitive_Requirements.md)

---

## Goal

Ship **one visual language** (UDS) across every domain suite, then deepen features against top-3 tools per domain. Marketplace lists **suites only** — no thin legacy plugins.

---

## Inventory (source of truth)

| Keep (domain) | Role |
|---------------|------|
| `uds-suite` | Reference chrome + diagnose depth |
| `dbc-studio` | Database authoring |
| `eds-studio` | EDS / DCF authoring |
| `canopen-suite` | CANopen network / OD |
| `j1939-suite` | Heavy-duty PGN / DM |
| `obd-suite` | OBD-II scanner |
| `autosar-suite` | COM / ARXML / CanNm / E2E / SecOC |
| `ethercat-suite` | ESI / topology / PDO / CoE / DC / datagrams |
| `ai-agent` | Platform agent (market optional) |

| Removed / retired | Status |
|-------------------|--------|
| Thin single-purpose plugins (UDS×N, DBC×N, …) | Gone from `plugins/` and `market.json` plugins[] (S5). Do not reintroduce. |
| `tx-lab`, `bus-security`, `protocol-hub`, `log-analysis`, `bus-utilities` | Archived under `plugins/_retired/`; skipped by PluginManager; not in `SUITE_IDS` / market. |

Pack command: `python scripts/plugin_tool.py pack-suites`  
Local market: `python scripts/make_market.py`

---

## Phases

### P0 — Chrome parity (this pass)

1. Document UDS design system + competitive requirements.  
2. Shared helpers: `vscode_theme`, `codicons`, `suite_chrome`.  
3. Every suite `app_shell.py`: theme, SuiteNav icons, zero outer margins, SuiteContent, flat SuiteToolbar (no GroupBox soup), flat OUTPUT host.  
4. AI Agent window adopts the same theme.  
5. Repack market OPKs so installers get the new chrome.

**Exit:** Opening any suite feels like UDS (nav + surface), even if page bodies still need polish.

### P1 — Highest daily use after UDS

Order: **dbc-studio → canopen-suite**  
Apply page-level rules (block() sections, StepSpin, Setup-only connection where needed) and close competitive P0 rows in the requirements doc.

### P2 — Protocol scanners

**j1939-suite → obd-suite → autosar-suite → ethercat-suite**  
Add missing Setup / OD / DM / Mode / COM coverage from competitor tables.

### P3 — (retired hubs)

Former P3 hubs (`protocol-hub`, `log-analysis`, `bus-security`, `bus-utilities`, `tx-lab`) are archived; do not reintroduce as product tiles. Fold useful pieces into a real domain suite only via PDCA.

### P4 — Ship

Bump suite versions, `make_market.py`, smoke install from market, README cleanup of any leftover thin-plugin or retired-hub names.

---

## Agent rules while executing

- English only in non-`.md` files.  
- Prefer measurement / user hotspots over drive-by page rewrites.  
- After landing a phase item: update the status table in `Domain_Suite_Competitive_Requirements.md`.  
- Never add thin plugins back to `SUITE_IDS` / `SUITE_CATALOG`.

---

## Decision log

| Date | Decision |
|------|----------|
| 2026-09-20 | UDS chrome is the gold standard for all suites. |
| 2026-09-20 | Legacy thin plugins already purged; market plugins[] = suites + ai-agent only. |
| 2026-09-20 | P0: apply theme/nav/flat toolbar/OUTPUT host to all suite shells + AI Agent. |
