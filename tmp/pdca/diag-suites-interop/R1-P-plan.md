# R1-P · Plan — Cross-page interop + context menus

## Gaps

- OBD / J1939 / UDS have Context Next + partial `set_focus` write, but no right-click, weak focus consume, no `goto_*_target`.
- Norm §13.2 + mainstream tools (CANoe / CANdb++ / OBD scanners): Copy ID, Open related view, Send/Poll from selection.

## Goals

1. Right-click on main work lists with Copy + Related + one primary verb.
2. Focus typed fields + `on_focus`; target page highlights selection.
3. Shell bridges: `goto_pid` / `goto_pgn` / `goto_did` / `goto_service` (or equivalent run_action).

## Acceptance

- [ ] OBD Scanner + Readiness menus; `obd.goto_pid`; focus highlight PID row
- [ ] J1939 Live + Transport/DM/Network menus; `j1939.goto_pgn` selects; sister Open Live/DM/TP
- [ ] UDS Services/DID/DTC/Scan menus; focus did/service; `uds.goto_did` / `uds.goto_service`
- [ ] Tests assert CustomContextMenu / goto_* / on_focus symbols; session focus notify
- [ ] English-only; no chrome regression

## Risks

- Diagnose.py is large — menus only, no layout rewrite.
- Live tables rebuild on timer — selection restore by ID after refresh.

## Out of scope

Autosar/EtherCAT; drag-drop; rewriting Context Next stages.
