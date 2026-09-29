# R1-C · Test

## Automated

```
python tests/test_session_focus.py  → PASS (set_focus, next_hint, _has_pdo_maps, next_step_bar)
python tests/test_shell_routes.py   → PASS
python tests/test_interop.py        → PASS
python tests/test_eds_pdo.py        → PASS
```

## Manual (smoke)

1. New session → Context Next = "New EDS…"
2. Open/create draft dirty → Next = Save
3. Saved draft without PDO maps → Next = Map PDOs
4. Objects select → Map chip → PDO OD list focuses same index
5. Profiles Insert → Dictionary opens; Map in PDO available on Profiles row
