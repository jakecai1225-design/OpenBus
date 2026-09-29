# R1-D · Do — PDO Map UX2

**Engineering · 2026-09-29**

## Flow simplify (`eds_pdo.py`)

- Headers: **Slots** | **Objects** (short verbs).
- Add / Remove / Import → left header icon tools (no footer).
- Removed Map button + dual caption lines; **one** status line.
- **Click** OD row → map (when slot ready).
- Empty copy: Import → select → click.

## Splitter (`_ui.configure_splitter` + page)

- Opaque resize, handle 6px, stretch (1,1), Preferred policies, lower mins.
- Viewport-based `setSizes` after show (`_fit_split_sizes`).
- Overlay QSS handle width 6px.

## Tests

`python plugins/canopen-suite/tests/test_eds_pdo.py` → PASS.
