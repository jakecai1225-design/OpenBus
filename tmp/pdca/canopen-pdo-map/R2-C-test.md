# R2-C · Test — CANopen EDS PDO Map

**Role:** Test  
**Date:** 2026-09-29

## Automated

All R1 cases + `test_format_budget_and_first_slot` → **PASS**.

## Manual (human)

1. Empty PDO map → Import → first empty map slot selected; OD filter focused; status toast.  
2. Click Comm row → selection does not stick / cannot map.  
3. Budget shows `· N free`.  
4. Re-run 64-bit block from R1 script.

## Residual

GUI steps not executed in agent environment.
