# R1-D · Do — CANopen chrome polish

**Role:** Engineering  
**Date:** 2026-09-29

## Changes

| File | What |
|------|------|
| `app_shell.py` | File → Recent EDS / Project EDS menus + fill helpers |
| `workspace_sidebar.py` | `build_eds_sidebar` flat edit leaves only |
| `eds_pdo.py` | Removed Fine-tune UI; OD list scroll/elide |
| `analysis.py` | Slim Trace: tool strip + Trace\|Watch |
| `_ui.py` | `style_list` / `style_tree` scroll + elide |
| `tests/test_ux_simplify.py` | Contract tests |

## Verify

`python tests/test_ux_simplify.py` (+ shell / eds_pdo / interop) → PASS.
