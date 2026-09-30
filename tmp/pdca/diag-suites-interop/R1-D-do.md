# R1-D · Do

## Changes

### OBD
- `session.set_focus` + focus notify; `goto_pid_target` / `goto_readiness_target`
- Scanner: context menus on Live / Freeze / DTC; `select_pid`; focus restore on refresh
- Readiness: context menu Copy / Open Scanner / Discover

### J1939
- Live PGN tree: context menu Copy / RQST / Open Diagnostics|Transport; `select_pgn` + focus highlight
- views: shared `_host` menus + PGN UserRole; DM headers include real PGN col
- `goto_pgn_target` / `j1939.goto_diagnostics`

### UDS
- Session focus: leaf + service + did + dtc + `on_focus`
- Shell: `goto_service_target` / `goto_did_target` / `goto_dtc_target`
- diagnose: Services / DID / DTC context menus + select_*
- scan: row context Apply / Copy / Open Services|DID

### Tests
- Interop symbol + focus notify contracts in three suites

English-only; live `plugins/` path.
