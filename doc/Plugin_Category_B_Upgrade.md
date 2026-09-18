# Plugin Category B Upgrade (all remaining tools)

**Predecessor:** [Plugin_P0_Competitive_Upgrade.md](Plugin_P0_Competitive_Upgrade.md) (Wave A / P0 — done).  
**Bar:** same as A — core feature parity + solid UX (English UI, `_shared` shell/state, persist, export).  
**Order:** Diagnostic → Database → Protocol → Security → Tools.

## Pattern (copy from A)

1. `from _shared import plugin_shell, state_store` (+ `dbcparse` / `dbc_picker` / `isotp_client` when needed).  
2. English window title, buttons, tabs, status, help strip.  
3. `plugin_shell.bind_raise` + English `register_command`.  
4. Persist last paths/settings via `state_store`.  
5. CSV/ASC/JSON/HTML export where the tool produces data.  
6. No new vendored `dbcparse` copies.

## Already done (Wave A)

uds-diagnostic, can-dashboard, can-frame-generator, can-simulator, j1939-analyzer, obd2-scanner, dbc-lint, dbc-diff, trigger-logger.

## Wave B inventory

### Diagnostic
| Plugin | Competitors | Core bar |
|--------|-------------|----------|
| uds-batch | Softing batch / CANoe DiVa lite | CSV/JSON runner + stats persist + ISO-TP shared |
| uds-scan | Softing scan / TSMaster ECU map | ID/service probe map + export + profile hints |
| uds-security-audit | pentest UDS tools | Seed entropy, timing, NRC stats + report |

### Database
| Plugin | Competitors | Core bar |
|--------|-------------|----------|
| dbc-codegen | cantools / CANdb++ codegen | C pack/unpack + workspace DBC + out dir persist |
| dbc-exporter | CANdb++ matrix export | CSV/JSON/HTML matrix + workspace DBC |
| dbc-merge | canmatrix merge | Multi-DBC merge + conflict policy + report |

### Protocol
| Plugin | Competitors | Core bar |
|--------|-------------|----------|
| autosar-nm-monitor | CANoe NM | NM parse + node state + CSV |
| canopen-scanner | CANopen Magic / Peak | SDO identity + heartbeat + export |
| iso-tp-monitor | CANoe TP Observer | FF/CF/FC reassembly + timeout + CSV |
| isobus-monitor | ISO 11783 tools | Address claim + nodes + CSV |
| nmea2000-decoder | Yacht Devices / Actisense | Fast Packet + PGN decode + CSV |
| gbt27930-monitor | China EV charge monitors | BMS/charger FSM + CSV |
| xcp-monitor | CANape / INCA lite | CTO/DAQ decode + session view |

### Security
| Plugin | Competitors | Core bar |
|--------|-------------|----------|
| can-fuzzer | Caring Caribou / custom fuzzers | Modes + rate limit + stats export |
| can-ids | IDS research tools | Learn whitelist + anomaly CSV |
| can-stress | load generators | Flood/malformed + load target + stats |
| e2e-checksum | AUTOSAR E2E profiles | Live CRC/alive check + fail log |

### Tools
| Plugin | Competitors | Core bar |
|--------|-------------|----------|
| can-bit-timing | PEAK/Kvaser calculators | Classic+FD presets + copy result |
| can-gateway | CANoe Gateway / SocketCAN gw | Rules persist + remap + rate limit |
| can-id-scanner | Busmaster / PCAN | Live ID map + DBC audit + CSV |
| can-quality-report | CANoe statistics | Load/period/HTML report |
| can-reverse | Kayak / bit-level tools | Heatmap + A/B + export |
| frame-compare | CANalyzer compare | Dual source + CSV |
| log-toolkit | asammdf / Vector tools | Trim/filter/merge + ASC/CSV |

## Acceptance (each plugin)

- [x] English UI + shell status/help  
- [x] `_shared` imports (no new dbcparse copy)  
- [x] state_store round-trip where applicable  
- [x] Core bar features  
- [x] Syntax OK / activate entry preserved (`ast.parse` all `plugins/*/main.py`)

## Progress

| Category | Status |
|----------|--------|
| Diagnostic | done (uds-batch, uds-scan, uds-security-audit) |
| Database | done (dbc-codegen, dbc-exporter, dbc-merge) |
| Protocol | done (autosar-nm-monitor, canopen-scanner, iso-tp-monitor, isobus-monitor, nmea2000-decoder, gbt27930-monitor, xcp-monitor) |
| Security | done (can-fuzzer, can-ids, can-stress, e2e-checksum) |
| Tools | done (can-bit-timing, can-gateway, can-id-scanner, can-quality-report, can-reverse, frame-compare, log-toolkit) |

**Wave B complete:** 24 remaining tools upgraded to the same bar as Wave A (9 P0). Combined coverage ≈ 33 functional plugins + `_shared`.
