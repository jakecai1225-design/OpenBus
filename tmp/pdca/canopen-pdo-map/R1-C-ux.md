# R1-C · UX eval — CANopen EDS PDO Map

**Role:** UX evaluation  
**Date:** 2026-09-29  
**Benchmarks:** `.cursor/rules/ui-ux-philosophy.mdc`, top-tier industrial IDE PDO editors (CANeds-like)

## Task success path

**Primary job:** put OD objects into PDO map slots.

| Step | Before R1 | After R1 Do | Verdict |
|------|-----------|-------------|---------|
| Find work surface | L tree / R OD OK | Same; more vertical for OD list | Improved |
| Know what to do | Caption rows “1./2.” | Tooltips + target line | Meets philosophy (hints not chrome) |
| Map | Activate OD / Map btn | Unchanged | Pass |
| Power edit | Always-open Fine-tune | Collapsed disclosure | Improved |
| Over-budget | Red text only | Block + toast | Foolproof ↑ |

## Philosophy checklist

- [x] Primary work (OD list + slots) gains space — Fine-tune not competing by default  
- [x] No stacked “instruction” caption rows  
- [x] One obvious path: select slot → pick OD  
- [ ] Still two footers (Add/Remove left, Map right) — acceptable for L/R IDE; watch R2 density  
- [ ] Comm vs Map still mixed in one tree — discoverability Med (R2 candidate)  
- [ ] No visual design system review of toggle styling vs SuiteIconTool (cosmetic)

## Score (1–5 vs top-tier)

| Dimension | Score | Note |
|-----------|-------|------|
| Clarity of primary path | 3.5 | Better without captions; target line still quiet |
| Chrome discipline | 3.5 | Fine-tune fold helps; left foot still busy |
| Error prevention | 4 | 64-bit gate |
| Density / beauty | 3 | Not yet top1; tree/list visual hierarchy next |
| Overall R1 | **3.4** | Direction correct; not done |

## UX recommendations → Act / R2

1. Auto-select first empty map slot after Import.  
2. Soften Comm rows (dim / non-selectable) or nest under “Comm” with collapsed default.  
3. Show remaining bits in budget (“16 free”) not only used.  
4. Map button label shorten when space tight (“Map”).
