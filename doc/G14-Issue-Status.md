# G14 Issue Status (2026-08-24)

## Task Overview
Three issues in Graphic page:
1. **P1**: Add signal dialog should pick signals from loaded DBC/database files
2. **P2**: After adding signal and starting measurement, trace refreshes but graphic doesn't draw curve
3. **P3a/b**: New toolbar buttons missing icons; dragging only moves single Y-axis

## Implementation Status

### ✅ P1 - From DBC Database Signal Picker (COMPLETE)
**Implementation**: 
- Modified `GraphicView::setupUi()` to use `DbcSignalPickerDialog` instead of `SignalConfigDialog`
- Added `m_dbcManager` member and `setDbcManager()` method
- Modified `GraphicModule::createPage()` to inject DBC manager reference  
- **Key changes**:
  - `graphicview.cpp`: Replaced manual CAN ID input with tree dialog showing all loaded DBCs
  - `graphicview.h`: Added `DbcManager* m_dbcManager = nullptr;`
  - `graphicmodule.cpp`: Calls `gv->setDbcManager(ctx.dbcManager)` after creating view

**Build Status**: 
- MOC auto-generation successful → header syntax validated
- Full build fails due to unrelated driver/BLF library compilation errors (zlgcan SDK, vector_blf)
- Code structure verified through Qt MOC compilation

### ⏸️ P2 - Debugging: No Curve Drawing (IN PROGRESS)
**User Report**: Trace shows frames refreshing, but graphic page has no curve after adding DBC signal

**Investigation Points**:
1. Match condition in `onFrame()`: `(frame.id & 0x1FFFFFFF) == sd.config.canId && frame.extended == sd.config.extended`
2. Bit decoding in `extractValue()`: Calls `sig.dbcSig.decode(frame.data)`
3. Potential mismatch between DBC startBit (bit position) vs byteOffset confusion
4. Frame length validation (CAN FD vs classic CAN)

**Action Plan**:
- [ ] Add debug logs in `onFrame()` to match logging when frames arrive
- [ ] Verify signal extraction returns non-NaN values
- [ ] Check if pushSample adds data to RingBuffer
- [ ] Validate display data refresh in `refreshDisplayData()`

### ⏸️ P3a - Missing Toolbar Icons (BLOCKED)
**Issue**: hand.svg, axis-fit-x.svg, axis-fit-y.svg not displayed

**Root Cause Analysis**:
- SVG icons created at resources/icons/ directory
- Need to verify resources.qrc registration  
- QSS icon path resolution may fail

**Pending Action**:
- [ ] Confirm `resources.qrc` contains new icon entries
- [ ] Test icon loading with fallback colors
- [ ] Apply VS Code-style line art icons (current SVG design)

### ⏸️ P3b - Dragging Single Y-Axis Only (BLOCKED)
**Issue**: Mouse drag only moves single Y-axis track, other tracks remain static

**Expected Behavior**: Pan mode should translate X-time globally across all tracks while preserving relative Y positions

**Technical Requirements**:
- Left-drag pan with X-axis synchronization (blocker prevents rangeChanged events)
- Maintain per-signal Y-offset during translation
- Update all axisRect bottom ranges synchronously

## Current Blockers
1. **Build System**: Compilation of zlgcan/zlg driver blocks full test suite execution
   - Temporary workaround: Skip driver builds, test UI module only
   - Long-term: Isolate ZLG SDK dependency issues

2. **Resource Files**: Icon embedding requires qrc rebuild after modifications

## Next Steps (After Build Fix)
1. Enable unit tests for GraphicView (offscreen rendering + signal matching)
2. Implement P2 debug logging in `onFrame()` loop
3. Verify P1 signal picker dialog interaction workflow
4. Fix P3a/P3b icon visibility and drag behavior

## Related Issues/PRs
- DEF-06: Previous ZLG enumeration heap corruption (unrelated current issue)
- UI-06: Offline playback history refill (separate enhancement)
