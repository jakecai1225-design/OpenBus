# R1-P · Plan — CANopen EDS PDO Map

**Role:** Design  
**Date:** 2026-09-29  
**Scope:** File-layer PDO map editor (`eds_pdo`), not Live PDO bus read.

## 1. Current state (as-is)

Layout: golden L/R split — left PDO slot tree + Add/Remove; right OD picker + Map button + permanent Fine-tune form. Empty → Import RPDO1+TPDO1 pack. Map path: select slot → click/activate OD item or Map button → dword written to draft.

Strengths: OD-first mapping, bit labels from data type, PDO-flag sort, budget label, empty_state, kit tokens.

## 2. Gap vs top-tier (industrial IDE)

| Area | Gap | Severity |
|------|-----|----------|
| Chrome | Caption rows (`how_left` / `how_right`) + always-open Fine-tune steal vertical space from OD list (philosophy: hints → tooltips; one job) | High |
| Feedback | Budget >64 only red text; mapping still allowed | High |
| Discoverability | Comm nodes look selectable but disable mapping without a quiet recovery path | Med |
| Structure | Pure logic mixed in page module; **zero** unit tests for parse/pack/bits/budget/write | High |
| Density | Footer Add/Remove OK; Fine-tune always visible competes with picker | Med |
| Live sync | Not in R1 (Live PDO is separate page) | Out |

## 3. R1 goals (in scope)

1. **Chrome diet:** remove step caption labels; fold Fine-tune behind a disclosure (collapsed by default).
2. **Bit budget gate:** refuse map/apply that would push used bits >64; status toast + budget stays red.
3. **Test foundation:** unit tests for `_parse_map`, `_pack_map`, `_bits_for_entry`, `_mappable_entries`, budget math helper (extract if needed).
4. **Comm clarity:** when Comm selected, keep OD list disabled and show one-line target hint only (no Fine-tune noise).
5. Keep primary path: select slot → pick OD (activate) → mapped.

## 4. Out of scope (R1)

- Live PDO page redesign  
- Drag-drop OD→slot  
- COB-ID / transmission type editors in this panel  
- Visual golden-ratio pixel audit (defer R2 if Check fails)

## 5. Acceptance criteria (R1)

| ID | Criterion | Measurable |
|----|-----------|------------|
| A1 | No permanent “1. / 2.” caption rows on L/R | Code review + UX checklist |
| A2 | Fine-tune collapsed by default; expand shows spins | Manual / property default |
| A3 | Mapping that would exceed 64 bits is blocked + status message | Unit + manual |
| A4 | `pytest` (or suite test runner) covers parse/pack/bits/mappable/budget | Green tests |
| A5 | Empty → Import → Add slot → Map OD still ≤3 obvious actions | UX eval |
| A6 | English-only in touched non-`.md` files | Grep |

## 6. Risks

- Collapsing Fine-tune may hide power users → disclosure + tooltip  
- Budget block may surprise when replacing a slot (must subtract current slot bits before check)

## 7. Do order

1. Extract/testable budget helper; write tests  
2. Gate `_write_slot` / map_from_od / apply_map  
3. UI chrome: captions off; Fine-tune disclosure  
4. Update STATUS → Check roles
