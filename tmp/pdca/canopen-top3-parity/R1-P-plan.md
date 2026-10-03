# R1-P — CANopen Top3 parity (Plan)

**Role:** Design  
**Date:** 2026-10-01  
**Benchmark:** emotas DeviceExplorer · Vector CANoe.CANopen · CANopenEditor  

## Goals

1. Document Top3 + Phase A/B/C in `doc/Domain_Suite_Competitive_Requirements.md` §2.  
2. Phase A: segmented SDO + abort/timeout UX; HB/EMCY health; Trace decode wired; journey CTAs; unit tests + soak script.  
3. Phase B: Live OD polish; PDO live viz leaf; DCF path; LSS lite; CiA 402 Drive panel.  
4. Phase C: codegen golden test; semantic Trace helpers; multi-node PDO link MVP in project.

## Acceptance (Round 1)

| ID | Criterion |
|----|-----------|
| A1 | `SdoClient` supports segmented upload for payloads >4 bytes (unit test) |
| A2 | Abort codes surface via `cia301_codes.sdo_abort_text` |
| A3 | `NetworkHealth` tracks HB age + EMCY ring; session exposes API |
| A4 | Trace/`EdsBusDecoder` still classifies EMCY/HB/SDO (existing + regression) |
| A5 | Soak checklist markdown exists under this PDCA folder |
| B1 | Live sidebar has Drive leaf; Drive page builds without Qt app crash (AST) |
| B2 | LSS encode helpers + Scan UI entry |
| B3 | DCF open/save remains via edsparse (`is_dcf` / ParameterValue) |
| C1 | Codegen golden fixture test (minimal OD) |
| C2 | `pdo_link` MVP pure functions + test |

## Risks

- Segmented SDO on real devices varies; keep expedited path default for ≤4 bytes.  
- Multi-node PDO link is MVP (draft writeback only), not PDL full product.

## Out of scope

HIL, CAPL, CANopen FD, eds-studio Diff/CI.
