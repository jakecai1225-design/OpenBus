# R1-D — Do (engineering)

**Date:** 2026-10-02

## Changes

- `core/sdo_client.py` — segmented upload/download, abort text, busy/timeout
- `core/network_health.py` — HB age, NMT label, EMCY ring
- `session.py` — wire health + poll timer + `sdo_download_bytes` + `health_summary`
- `pages/object_dict.py` — Network health row; bytes result; busy abort text
- `pages/pdo.py` — Live unpack column from RPDO/TPDO frames
- `pages/drive.py` — CiA 402 controlword/statusword lite
- `pages/network.py` — LSS lite stack page
- `pages/workspace_sidebar.py` — Live PDO / Drive / LSS leaves
- `app_shell.py` — routes + workspace stack for drive/lss
- `core/lss_master.py`, `core/pdo_link.py` — pure helpers
- `core/project.py` — `nodes` + `pdo_links` in manifest
- Tests: `test_top3_core.py`, `test_codegen_golden.py`, `test_top3_pages_ast.py`; shell routes updated

## Why

Close Top3 P0/P1 gaps for the Author→Commission→Observe→Deliver golden journeys without stealing eds-studio file-depth work.
