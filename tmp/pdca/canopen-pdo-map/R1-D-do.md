# R1-D · Do — CANopen EDS PDO Map

**Role:** Engineering  
**Date:** 2026-09-29  
**Plan ref:** `R1-P-plan.md`

## Changes

### `plugins/canopen-suite/pages/eds_pdo.py`

1. **Helpers:** `map_budget()`, `would_exceed_pdo_bits()` — bit accounting with replace-slot exclude.
2. **Gate:** `_write_slot` refuses writes that would exceed 64 bits; status toast.
3. **Chrome:** removed `how_left` / `how_right` caption rows; guidance → tooltips on tree / OD list / target.
4. **Fine-tune:** disclosure toggle, **collapsed by default**; form hidden until expanded.
5. Budget label uses `map_budget`.

### `plugins/canopen-suite/tests/test_eds_pdo.py`

New unit tests: parse/pack, classify, bits, mappable filter, budget / exceed.

## Verify

```
python plugins/canopen-suite/tests/test_eds_pdo.py
→ PASS eds_pdo helpers
```

## Not done (deferred to Act / R2)

- Drag-drop mapping  
- Comm-row UX polish beyond disabled editor  
- Pixel density / golden split re-measure  
- Live PDO page
