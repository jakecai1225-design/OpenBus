# R1-D · Do — DBC Studio norm (overall)

**Role:** Engineering  
**Date:** 2026-09-29

## Changes

- `app_shell.py`: 4 activities, workspaces, File menu, status Next, `run_action`
- `pages/workspace_sidebar.py`: Edit/Analyze/Integrate/Deliver leaves
- `document.py`: focus + `next_hint` / `mark_validated`
- `pages/_ui.py`: kit (from canopen, retitled)
- `pages/validate.py`: mark_validated + next_step Save/Export
- `codicons.py`: activity aliases
- Tests: `test_shell_routes.py`, `test_session_hint.py`

## Verify

All listed tests PASS.
