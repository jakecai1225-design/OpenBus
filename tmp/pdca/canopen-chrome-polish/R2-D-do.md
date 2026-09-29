# R2-D · Do — CANopen chrome polish

**Role:** Engineering  
**Date:** 2026-09-29

## Changes

1. Confirmed File menu empty states: `(empty)` / `(no project open)` / `(no EDS in folder)`.
2. Trace Watch: `mins={0: 88, 1: 96, …}` stretch Name column.
3. Dropped unused `QHBoxLayout` import in `eds_pdo.py`.

## Verify

`python tests/test_ux_simplify.py` → PASS.
