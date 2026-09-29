# R1-P · Plan — PDO Map UX simplify + real splitter resize

**Role:** Design  
**Date:** 2026-09-29

## Problem

1. Map flow still feels unclear / not simple enough (too much chrome: dual headers, target+budget lines, Map button row, Fine-tune, left footer).
2. Dragging the divider feels like one pane **covers** the other instead of **both widths adjusting**.

## Likely splitter causes

- High `minimumWidth` on right (300) + tree column floors → drag hits a wall then clips.
- Unequal stretch + content sizeHints fight live resize.
- Handle only 3px; need opaque resize + equal stretch + lower mins + size policies.

## R1 goals

### Flow (simpler)

One strip under page chrome mentally: **Slots | Objects**

- Merge target + budget into **one** status line on the right header (or left header trailing).
- Drop separate “Pick from Object Dictionary” verbose header → short **Objects**.
- Left header **Slots** + count; Import as icon/chip stays.
- Primary map action: **single-click** OD row maps when a slot is ready (keep double-click); Map button becomes secondary or inline in status (“Map” ghost).
- Move Add/Remove into left `panel_header` trailing (no bottom foot row).
- Fine-tune stays collapsed disclosure.

### Splitter

- `configure_splitter`: opaque resize on, handle ≥5px, stretch (1,1) for this page (equal drag share), mins 200/220.
- Explicit `Preferred` horizontal size policy on both panes; clear maximum width.
- After show, setSizes from current width once (not only 618/382 absolute that ignore viewport).

## Acceptance

| ID | Criterion |
|----|-----------|
| C1 | No bottom Add/Remove foot; actions in header |
| C2 | One status line (slot + bits free), not two caption labels |
| C3 | Single-click OD maps when slot ready |
| C4 | Dragging splitter resizes both panes live (opaque); neither paints over the other |
| C5 | Unit tests still pass; add splitter helper test if extracted |

## Out of scope R1

Live PDO; drag-drop OD onto tree.
