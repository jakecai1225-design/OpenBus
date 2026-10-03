# STATUS — canopen-top3-parity

| Field | Value |
|-------|--------|
| Round | 2 Accept passed |
| Phase | CLOSED |
| Goal | Boutique closed loop vs DeviceExplorer / CANoe.CANopen / CANopenEditor |

## Todos

- [x] Doc §2 + R1-P
- [x] Phase A implement + PDCA×2
- [x] Phase B implement + PDCA×2
- [x] Phase C implement + PDCA×2

## Shipped (summary)

| Area | Landing |
|------|---------|
| SDO | Segmented upload/download + abort/timeout/busy in `core/sdo_client.py` |
| Health | `NetworkHealth` in session; Live OD / Drive show HB+EMCY |
| Trace | Existing `EdsBusDecoder` + Live PDO unpack |
| Drive / LSS | `pages/drive.py`, LSS leaf on Network |
| DCF | Session open/save `.dcf` + ParameterValue |
| Codegen | Golden tests `tests/test_codegen_golden.py` |
| PDO link | `core/pdo_link.py` + project `nodes`/`pdo_links` |
