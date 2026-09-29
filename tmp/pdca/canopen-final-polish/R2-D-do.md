# R2-D · Do — CANopen final polish (local)

**Role:** Engineering  
**Date:** 2026-09-29

## Kit

- `_ui.py`: chrome recipe docstring; `FILTER_H=34`; `PANE_MIN` / `PANE_MIN_PROP`; `split_editor` uses tokens.

## Pages

| Page | Change |
|------|--------|
| `codegen.py` | No body tip; issues hidden until findings |
| `object_dict.py` | SDO hint → Value tooltip; pane mins tokens; Apply via next-step only |
| `eds_pdo.py` | Status one CTRL_H line; pane mins tokens |
| `eds_editor.py` | Tree/form mins → tokens |
| `library.py` | Drop blurb caption; profile tip on tree |
| `network.py` | Progress 2px; NMT hint → list tooltip; scan strip hide when populated |
| `analysis.py` | Pause height = CTRL_H |
| `app_shell.py` | Context Next max width 128 |

## Tests

`test_ux_simplify.py` + density contracts — PASS.
