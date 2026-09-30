# R1-P · Plan — UDS Suite norm (IA + session)

**Role:** Design  
**Date:** 2026-09-30

## Goals

1. Activities: Diagnose / Scan / Batch / Security + Setup footer (Profiles leaves File + Diagnose sidebar).
2. Diagnose sub-features as Side Bar leaves (Session, Services, DID, DTC, SecAccess, Flash, Profiles) — no editor Tab strip.
3. File menu for profile Open/Save/Recent; Context Next + `run_action` bus.
4. Session `set_focus` / `next_hint` golden path; Scan Apply bridges to Diagnose.

## Acceptance

- [ ] NAV_PAGES == diagnose, scan, batch, security, setup
- [ ] No Diagnose QTabBar on chrome
- [ ] File menu has profile I/O; profiles not a top activity
- [ ] next_hint advances (IDs → Extended → Services / Scan)
- [ ] Aliases (services, did, profiles, …) resolve
- [ ] Existing proto/profile tests PASS

## Out of scope

Page density (R2); UDS/ISO-TP algorithms.
