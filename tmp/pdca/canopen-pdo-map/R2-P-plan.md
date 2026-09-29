# R2-P · Plan — CANopen EDS PDO Map

**Role:** Design  
**Date:** 2026-09-29  
**Input:** `R1-A-act.md`

## Goals

1. After **Import pack**, auto-select the first **empty** mapping slot (or first map `#1`) and focus OD filter.  
2. Budget label: `N slot(s) · used/64 · M free` (red if used>64).  
3. **Comm** tree rows: not selectable for mapping (`ItemIsSelectable` off); only Map count / map entries selectable.  
4. Keep R1 gates; extend unit tests for budget remaining text helper if extracted.  
5. Document manual verify checklist for human (Accept may stay conditional on GUI).

## Out of scope

- Drag-drop, Live PDO, visual theme overhaul.

## Acceptance

| ID | Criterion |
|----|-----------|
| B1 | Import pack lands selection on a map slot ready for OD pick |
| B2 | Budget shows free bits |
| B3 | Comm rows cannot become current mapping target via click |
| B4 | R1 tests still pass; any new helpers covered |
| B5 | UX score ≥ 3.8 on clarity of primary path (eval role) |
