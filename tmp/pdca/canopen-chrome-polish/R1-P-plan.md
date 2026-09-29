# R1-P · Plan — CANopen chrome polish

**Role:** Design  
**Date:** 2026-09-29

## Gaps

1. Current / Recent / Project EDS lived in the EDS sidebar — competed with edit leaves.
2. PDO Fine-tune panel added a second path after click-to-map.
3. Objects list / Trace showed sticky or useless scrollbars (H overflow).
4. Trace mixed decode viewing with NMT / Quick send chrome.

## Goals

- Files under **File** menu; sidebar = edit leaves only.
- One map path: select slot → click object.
- Scrollbars only when content overflows; elide long names.
- Trace = filter + Pause + Import/Clear/Export + Trace|Watch.

## Acceptance

- [ ] File menu has Recent EDS and Project EDS; sidebar has no file folds.
- [ ] No Fine-tune / fine_toggle in `eds_pdo.py`.
- [ ] OD list H-scroll off + ElideRight; Trace vertical scroll AsNeeded.
- [ ] No Quick send / raw TX on Trace page.
- [ ] `test_ux_simplify.py` PASS.

## Out of scope

- Host C++ chrome; market pack; Live OD redesign.
