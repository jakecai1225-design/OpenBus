# Editor Docking (tabs / float / split)

> **Status**: Landed 2026-09-17  
> **Code**: `src/ui/spliteditorarea.h/.cpp`

## Goal

Industrial-style editor chrome so users can watch several pages at once (esp. multi-monitor): Trace + Graphic + Flow side by side or on separate screens.

## Interactions

| Action | Result |
|--------|--------|
| Drag tab off its tab bar over a pane | Drop overlay: **Center** (stack) / **Left·Right·Top·Bottom** (split) |
| Drop outside the main editor | Floating `DetachedTabWindow` on the screen under the cursor |
| Drag float window’s tab onto main | Same overlay; drop docks / splits |
| Double-click main tab | Detach to new window |
| Double-click float title (or float tab) | Merge into active main pane |
| Close float window | Merge back (content not destroyed) |
| Context menu | Split right / down, Move to new window, pin/close |

## Notes

- Shell left/right/bottom `QDockWidget`s are unchanged (tool panels).
- Layout tree is not yet persisted in the project file (follow-up).
- Escape cancels an in-progress dock drag.
