# R1-P · Plan — CANopen final polish (overall)

**Role:** Design  
**Date:** 2026-09-29

## Goals

1. Golden-path Context Next never stalls after Apply.
2. Remove Project overview from EDS stack; File menu owns projects.
3. Retire dead Setup page wiring.
4. One Apply-draft path on Live OD.
5. Clarify Live PDO vs EDS PDO copy; refresh stale pillar wording.

## Acceptance

- [ ] `next_hint` after OD: Trace → then Codegen (or NMT) — not stuck on Scan forever
- [ ] EDS workspace stack has no `project` page
- [ ] Setup not built / not reachable as a page
- [ ] Live OD: Apply only in next-step (or empty); header has status only
- [ ] Live PDO empty/status says "Live mapping (bus)"
- [ ] plugin.json + sidebar comments match four pillars
- [ ] Tests: session focus, shell routes, ux simplify PASS

## Out of scope

IA remap; host chrome; multi-EDS rename UI; Round 2 density (R2).
