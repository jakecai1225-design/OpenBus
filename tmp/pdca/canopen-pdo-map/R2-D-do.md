# R2-D · Do — CANopen EDS PDO Map

**Role:** Engineering  
**Date:** 2026-09-29  
**Plan:** `R2-P-plan.md`

## Changes (`eds_pdo.py`)

1. `format_budget_text` — shows free bits.  
2. `first_ready_map_slot` — empty slot preferred, else first map entry.  
3. `import_pack` — after refresh, auto-select ready slot + focus OD filter + status.  
4. Comm rows + `#0 · count` — not selectable; Comm dimmed + tooltip.

## Tests

Extended `test_eds_pdo.py` with `test_format_budget_and_first_slot`.  
`python plugins/canopen-suite/tests/test_eds_pdo.py` → **PASS**.
