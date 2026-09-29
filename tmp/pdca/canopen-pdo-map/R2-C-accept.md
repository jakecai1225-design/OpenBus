# R2-C · Accept — CANopen EDS PDO Map

**Role:** Acceptance  
**Date:** 2026-09-29

| ID | Criterion | Result |
|----|-----------|--------|
| B1 | Import auto-selects map slot | **PASS** (code) |
| B2 | Budget shows free bits | **PASS** |
| B3 | Comm not selectable for mapping | **PASS** (code) |
| B4 | Tests green | **PASS** |
| B5 | UX primary-path ≥ 3.8 | **PASS** (eval) |

## Decision

**PASS (process gate).** Two PDCA rounds complete for **EDS PDO Map**.

**Human reopen:** run `R2-C-test.md` manual once in OpenBus; file defects as a **new** requirement if visual issues remain.

Requirement status → **CLOSED (agent + unit tests)**; GUI sign-off optional follow-up.
