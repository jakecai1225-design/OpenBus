# R2-P — Plan (polish round)

**Date:** 2026-10-02

## Goals

1. Live PDO: match `pdo_label` to `classify_cob` kind exactly.
2. Project manifest round-trip for `nodes` / `pdo_links`.
3. Re-run full automated suite; Confirm soak doc linked from STATUS.
4. No new chrome layers; no CJK in code.

## Acceptance

Same A1–C2 as Round 1, plus:

| ID | Criterion |
|----|-----------|
| R2-1 | `test_pdo_link_apply` PASS |
| R2-2 | Project save/load keeps `pdo_links` |
| R2-3 | Shell routes include drive/lss/pdo |
