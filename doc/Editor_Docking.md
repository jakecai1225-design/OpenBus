# Editor Docking (tabs / float / split)

> **Status**: Landed 2026-09-17 (drag-split fix + project persistence)  
> **Code**: `src/ui/spliteditorarea.h/.cpp` · project `tabs.layout` / `window.*`

## Goal

Industrial-style editor chrome so users can watch several pages at once (esp. multi-monitor): Trace + Graphic + Flow side by side or on separate screens.

## Interactions

| Action | Result |
|--------|--------|
| Drag tab **down into the editor** (or past the tab strip) | Drop overlay: **Center** (stack) / **Left·Right·Top·Bottom** (split) |
| Drop outside the main editor | Floating `DetachedTabWindow` on the screen under the cursor |
| Drag float window’s tab onto main | Same overlay; drop docks / splits |
| Double-click main tab | Detach to new window |
| Double-click float **title bar** | Merge into main pane (no inner tab strip) |
| Close float window | Merge back (content not destroyed) |
| Context menu | Split right / down, Move to new window, pin/close |
| Escape | Cancel in-progress dock drag |

Need **two or more tabs** to see a lasting side-by-side split (dragging the only tab to an edge keeps one pane after cleanup — same as VS Code).

## Persistence (project JSON v4)

| Key | Content |
|-----|---------|
| `tabs.open` | Flat title list (compat) |
| `tabs.layout` | Splitter tree + `detached[]` geometries + `active` |
| `window.geometry` | Main window `saveGeometry` (base64) |
| `window.state` | Dock chrome `saveState` (base64) |

## Hard rule: Trace is one page

Trace’s bottom **Detail / Statistics / Diff / Signals** live inside `TraceTab`
(`#TraceExplorer`). They are **not** editor tabs.

`SplitEditorArea` only manages panes tagged `objectName == "EditorTabPane"`.
Internal `QTabWidget` / `QSplitter` under a page must never be harvested,
split, or floated by the shell docking code.

