# STATUS — strip / tab / StepSpin bottom-edge clip

**Phase:** CLOSED
**Date:** 2026-09-30

## Root cause
`SuiteToolStrip` `border-bottom` is painted *inside* the fixed height, so
`TOOL_H = CTRL_H + 2*STRIP_PAD_V` (36) left only 27px for 28px fields →
bottom borders clipped. Same class of bug for editor tabs (accent border)
and page underline tabs (Seeds / NRC / Findings).

## Fix
- `TOOL_H = CTRL_H + 2*STRIP_PAD_V + STRIP_EDGE` → **38** (shared + canopen + dbc)
- `CHROME_H` / `TAB_H` → 38 / 36; selected tab keeps bottom padding
- `SuiteTopTabs`: more bottom padding so underline clears glyph baseline
- StepSpin: outer owns border at CTRL_H; inner = CTRL_H-2
- tool_strip sizes StepSpin / QCheckBox to CTRL_H
- Input padding balanced to `2px 8px`

## Verify
Reactivate UDS Security / Scan — field boxes show full four borders;
Seeds underline clears text.
