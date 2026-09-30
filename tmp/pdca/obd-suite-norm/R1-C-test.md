# R1-C · Test — OBD Suite norm

**Role:** Test  
**Date:** 2026-09-30

## Automated

```
python plugins/obd-suite/tests/test_shell_routes.py
python plugins/obd-suite/tests/test_import_smoke.py
```

Result: PASS (nav keys, File/Next symbols, density inequality, session hint, tool_strip, AST/CJK)

## Manual gold path

1. Open OBD Suite → Scanner (not Log)
2. Context Next → Setup → Apply IDs
3. Next → Scanner → Discover
4. Next → Readiness → Read monitors
5. Ctrl+J toggles OUTPUT; no Log activity
