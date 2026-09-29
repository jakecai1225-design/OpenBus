# R1-C · Accept — CANopen EDS PDO Map

**Role:** Acceptance  
**Date:** 2026-09-29  
**Plan criteria:** `R1-P-plan.md` §5

| ID | Criterion | Result | Evidence |
|----|-----------|--------|----------|
| A1 | No permanent 1./2. caption rows | **PASS** | Captions removed; tooltips only |
| A2 | Fine-tune collapsed by default | **PASS** | `form_host.setVisible(False)`; toggle unchecked |
| A3 | >64 bits blocked + message | **PASS** (logic) | `would_exceed_pdo_bits` + `_write_slot` gate; unit tests. GUI toast not human-verified |
| A4 | Automated tests green | **PASS** | `test_eds_pdo.py` |
| A5 | ≤3 obvious actions empty→map | **PASS** (design) | Import → select/Add → Map. Human path not re-timed |
| A6 | English-only in code | **PASS** | Touched files English |

## Round 1 decision

**Conditional PASS** — criteria met in code + unit tests; GUI manual script pending human.

**Requirement NOT complete** — Round 2 required per gate. Feed Act backlog into R2-P.
