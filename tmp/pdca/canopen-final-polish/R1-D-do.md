# R1-D · Do — CANopen final polish (overall)

**Role:** Engineering  
**Date:** 2026-09-29

## Changes

| Area | Change |
|------|--------|
| `session.py` | `next_hint` after OD: Trace → NMT Start → Codegen; `advance_next_hint` / `reset_post_od_hint` |
| `app_shell.py` | Advance hint on Context Next; `view.trace`; drop Project from EDS stack; prune duplicate library key |
| `object_dict.py` | Single Apply path via next-step; header status only |
| `pdo.py` | "Live mapping (bus)" empty/status copy |
| `project.py` / `setup.py` | Retired stubs (File / Preferences) |
| `workspace_sidebar.py` / `plugin.json` | Four-pillar wording |
| Tests | `test_session_focus`, `test_shell_routes` extended |

## Verify

All listed suite tests PASS.
