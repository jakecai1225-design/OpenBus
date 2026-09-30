# R1-C · Test

## Automated

```
python plugins/obd-suite/tests/test_shell_routes.py   → PASS (incl. interop/focus)
python plugins/j1939-suite/tests/test_shell_routes.py → PASS (incl. interop)
python plugins/uds-suite/tests/test_shell_routes.py   → PASS (incl. interop)
python plugins/uds-suite/tests/test_session_hint.py  → PASS (focus notify)
```

AST parse of touched pages: PASS.

## Manual script (live OpenBus)

1. OBD: Discover → right-click PID → Copy / Poll / Open Readiness → Next still works
2. J1939: Live row → right-click → Open Diagnostics → focus PGN highlighted when back
3. UDS: Services → right-click 22 → Open DID; DID row → Open Services (22); Scan row → Apply

## Result

Automated: **PASS**. Manual: pending user verify after reactivate suites.
