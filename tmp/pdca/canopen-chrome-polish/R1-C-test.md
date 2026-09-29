# R1-C · Test — CANopen chrome polish

**Role:** Test  
**Date:** 2026-09-29

## Automated

```
python tests/test_ux_simplify.py   → PASS (4)
python tests/test_project_sidebar.py → PASS
python tests/test_shell_routes.py → PASS
python tests/test_eds_pdo.py → PASS
python tests/test_session_focus.py → PASS
python tests/test_interop.py → PASS
```

## Manual (human)

1. File → Recent EDS / Project EDS open drafts.
2. EDS sidebar: Objects / Profiles / PDO map / Check only.
3. PDO map: click object maps; no Fine-tune panel.
4. Long OD names elide; vertical scroll works when list is long.
5. Trace: filter + Pause + Import/Clear/Export; Watch updates; no Quick send.
