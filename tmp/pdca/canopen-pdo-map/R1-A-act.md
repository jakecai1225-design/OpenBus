# R1-A · Act — CANopen EDS PDO Map

**Role:** Act / orchestrator  
**Date:** 2026-09-29

## Keep

- Bit budget helpers + write gate  
- Collapsed Fine-tune  
- Caption removal  
- Unit test module

## Fix in R2 (from Check)

1. Auto-select first empty mapping slot after Import pack.  
2. Budget copy: show **remaining** bits (“N free”).  
3. Comm rows: non-selectable or visually dimmed; keep Map entries as only mapping targets.  
4. Optional: Qt smoke for Fine-tune default hidden (if harness available).  
5. Human manual script from `R1-C-test.md` before R2 Accept can be unconditional.

## Drop / defer beyond R2

- Drag-drop OD→slot  
- Live PDO redesign  
- Full visual redesign of tree chrome

## Round 2 Plan seed

Focus R2 on **foolproof post-Import path** + **Comm/Map clarity** + **budget remaining** + close manual verification notes.
