# R1-P · Plan — J1939 Suite norm (shell)

**Role:** Design  
**Date:** 2026-09-30

## Goals

1. Keep activities: Live / Transport / Diagnostics / Network (≤5).
2. File menu owns Load DBC / Recent; chrome title + Context Next.
3. `run_action` + session `next_hint` / `set_focus`.
4. `_ui` density tokens (TOOL_H=36); page `tool_strip` on views.
5. Contract tests for routes / File / Next / no CJK.

## Acceptance

- [ ] NAV_PAGES == analyzer, transport, diagnostics, network
- [ ] File menu Load DBC; `run_action("j1939.load_dbc")`
- [ ] Context Next advances via session.next_hint
- [ ] `tests/test_shell_routes.py` PASS

## Out of scope

New PGN decode tables; EtherCAT-style tab residency (single leaf per activity).
