# R1-C — Test

**Role:** Test  
**Date:** 2026-10-02

## Automated

```
python plugins/canopen-suite/tests/test_top3_core.py
python plugins/canopen-suite/tests/test_codegen_golden.py
python plugins/canopen-suite/tests/test_top3_pages_ast.py
python plugins/canopen-suite/tests/test_shell_routes.py
python plugins/canopen-suite/tests/test_eds_decode.py
```

Results: PASS (local 2026-10-02).

## Manual soak checklist

See `soak-checklist.md` — hardware optional for unit gates; soak required for Accept A5.

## Coverage map

| Plan ID | Covered by |
|---------|------------|
| A1 | `test_segmented_upload_roundtrip` |
| A2 | `test_decode_abort` |
| A3 | `test_network_health_hb_emcy` + session AST |
| A4 | `test_eds_decode` regression |
| A5 | soak-checklist.md |
| B1–B3 | Drive AST, LSS encode, DCF via edsparse path |
| C1–C2 | codegen golden + pdo_link tests |
