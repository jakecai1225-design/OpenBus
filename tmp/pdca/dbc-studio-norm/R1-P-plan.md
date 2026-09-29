# R1-P · Plan — DBC Studio norm (overall)

**Role:** Design  
**Date:** 2026-09-29

## Goals

1. Four activities + flat sidebars (Messages / … / Library).
2. File menu for New/Open/Save/Recent/Workspace/Reload; chrome row = title only.
3. Status: path · dirty · Context Next; `run_action` bus.
4. Document focus + `next_hint` golden path; empty CTAs on Editor/Validate/Export.

## Acceptance

- [ ] NAV_PAGES length == 4
- [ ] No document Open/Save cluster on editor chrome row
- [ ] next_hint advances: new/open → validate → save → export
- [ ] Deep-link aliases (editor, validate, …) still resolve
- [ ] Tests PASS

## Out of scope

Page density (R2); lint/diff algorithms.
