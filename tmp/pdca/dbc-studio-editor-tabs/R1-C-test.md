# R1 Check — Test

## Automated

```
python tests/test_shell_routes.py   # PASS (incl. normalize_open_tabs)
python tests/test_session_hint.py   # PASS
python tests/test_serialize_roundtrip.py  # PASS
python tests/test_bit_layout.py     # PASS
```

## Manual script

1. Activate DBC Studio (source `plugins/`).
2. Edit → Messages open as tab.
3. Click Value tables / Attributes → tabs accumulate; switch by tab.
4. Analyze → Matrix → new tab; close Matrix → prior tab active.
5. Close all → Messages reopens.
6. Restart suite → open_tabs restored when state present.
