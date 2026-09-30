# R1-D · Do — UDS Suite norm IA

**Role:** Engineering  
**Date:** 2026-09-30

## Changes

- `app_shell.py`: NAV Diagnose/Scan/Batch/Security/Setup; workspace stacks; File menu Profiles; `run_action`; Context Next; side_bar_enabled.
- `pages/diagnose.py`: Tab strip removed; `leaf_pages` dict for Session/Services/DID/DTC/SecAccess/Flash.
- `pages/workspace_sidebar.py`, `pages/_ui.py` (tokens + chrome helpers).
- `session.py`: `set_focus` / `next_hint` / `note_scan_hits` / `_ids_touched`.
- Scan Apply → `uds.apply_scan_ids`; Profiles → Batch via `run_action`.
