# Plugin P0 Competitive Upgrade

**Bar:** core feature parity + solid UX (not full CANoe/Softing).
**Order:** P0 nine plugins after shared foundation.

## Shared foundation

| Module | Path | Role |
|--------|------|------|
| Shell | `plugins/_shared/plugin_shell.py` | Status bar, empty state, CSV export, shortcuts, raise |
| State | `plugins/_shared/state_store.py` | JSON under `project/plugins/<id>/` |
| DBC picker | `plugins/_shared/dbc_picker.py` | Workspace DBC list + browse |
| DBC parse | `plugins/_shared/dbcparse.py` | Shared parser + BA_ attributes |

Host: `scripts/sin_host.py` adds `SIN_PLUGINS_DIR` (and plugin parent) to `sys.path` so `from _shared import …` works.

## Competitor map (P0)

| Plugin | Competitors | Core bar |
|--------|-------------|----------|
| uds-diagnostic | Softing DTS Monaco, CANoe Diag Console, TSMaster UDS | ECU profile, sequence runner, persist, EN UI chrome |
| can-dashboard | CANoe Instrumentation, PCAN-Explorer Panel | Layout save, LED/sparkline, host DBC |
| can-frame-generator | PCAN-Explorer Tx, CANoe IG | Per-row period, FD flags, signal panel |
| can-simulator | CANoe Restbus / IG | Per-msg enable+cycle, node filter, save |
| j1939-analyzer | PCAN J1939 add-in, CANoe J1939 | J1939 DBC, RQST, TP PDU tab |
| obd2-scanner | Torque/FORScan, CANoe OBD | ISO-TP, Mode 02, PID history |
| dbc-lint | CANdb++ checks | Rules/suppress, CI JSON/SARIF |
| dbc-diff | CANdb++ compare | Attr/VAL_/comment + HTML |
| trigger-logger | CANalyzer trigger, PCAN Trace | Signal conditions, time post, ASC |

## Acceptance checklist

### Shared
- [x] `_shared` importable from a loaded plugin (`SIN_PLUGINS_DIR` + plugin parent on path)
- [x] state_store round-trip under project or temp

### can-dashboard
- [x] English UI; empty/status via shell
- [x] Layout JSON save/load
- [x] LED + sparkline widgets; workspace DBC
- [x] Snapshot CSV export

### can-frame-generator
- [x] Per-row scheduler; inline edit; FD BRS/ESI
- [x] Signal editor; Ctrl+S JSON

### can-simulator
- [x] Per-message enable + cycle; node filter; save/load

### uds-diagnostic
- [x] ECU profile load/save; sequence tab
- [x] English chrome (tabs/toolbar); existing tests green; `tests/test_profile_seq.py` added

### obd2-scanner
- [x] ISO-TP; Mode 02 freeze frame; PID history

### j1939-analyzer
- [x] External DBC SPN decode; RQST; TP PDU tab

### trigger-logger
- [x] Signal conditions; time post; ASC export; multi-trigger OR

### dbc-lint
- [x] rules.json; `--ci` JSON/SARIF; workspace DBC

### dbc-diff
- [x] BA_/VAL_/comment diff; HTML export

## Manual soak (hardware / bus)

When hardware is available, tick live soak for each P0 plugin against the competitor “core bar” column above.
