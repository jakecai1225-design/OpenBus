# R1-C · Test — CANopen EDS PDO Map

**Role:** Test  
**Date:** 2026-09-29

## Automated

| Case | Result |
|------|--------|
| `test_parse_pack_roundtrip` | PASS |
| `test_classify_pdo_ranges` | PASS |
| `test_bits_for_entry_dtype` | PASS |
| `test_mappable_skips_pdo_area_and_shells` | PASS |
| `test_map_budget_and_exceed` | PASS |

Command: `python plugins/canopen-suite/tests/test_eds_pdo.py` → **PASS eds_pdo helpers**

## Manual script (human / reopen suite)

1. Open EDS → Dictionary → PDO map (empty) → Import pack → split appears.  
2. Select TPDO1 Map `#1` (or Add slot) → double-click Statusword-like OD → slot fills.  
3. Expand Fine-tune → confirm spins match; collapse → OD list taller.  
4. Fill mappings until near 64 bits → map another large object → **blocked**, status message, no draft change.  
5. Replace an existing large slot with a smaller OD → allowed.  
6. Select Comm row → Map button / OD disabled; target explains Dictionary for COB-ID.

## Gaps for Act / R2

- No Qt widget test for Fine-tune default visibility (requires QApplication harness).  
- Manual block steps not executed in this agent run (no GUI).
