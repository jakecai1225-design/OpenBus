# CANopen Suite — Product & UX continuity blueprint

**Role:** Product design + UX  
**Date:** 2026-09-29  
**Principle:** Every page ends with an obvious **next verb**. Journeys must be page-continuous, operation-continuous, process-continuous.

## Golden journeys

1. **EDS author:** New/Open → Profiles → Dictionary → PDO map → Validate → Save → Apply → Live OD  
2. **Commissioning:** Scan → Apply Node-ID → Live OD / NMT  
3. **Analyst:** Open EDS → Analysis → Import/Trace  
4. **Firmware:** Validate → Codegen → Save C/H  

## Continuity law (persistent)

- Empty / success / blocked states always offer **1–2 CTAs** that call `goto_page` / `run_action`.  
- Explorer lists the golden path as flat leaves (no “advanced” hide for PDO/Codegen).  
- No second tab strip under Network chrome.  
- Status text alone is not a handoff.

## Sprint backlog (this PDCA)

See `STATUS.md`. Full gap table in design review notes below.

### Gaps (severity)

| Break | Sev |
|-------|-----|
| Get started skips PDO / Validate / Save | H |
| Validate has no Save/Apply/Codegen buttons | H |
| Codegen missing from EDS explorer | H |
| PDO map nested as advanced | H |
| Analysis no Open-EDS empty CTA | H |
| PDO map no exit to Validate | H |
| Network inner Scan\|NMT tab bar | M |
| Live OD draft vs applied muddy | M |

## Success metrics

- Dead-end empty states on J1–J4 entry pages = 0  
- Find PDO map & Codegen from EDS sidebar without Help  
- Validate 0 errors → Save/Apply/Codegen in ≤2 clicks  
