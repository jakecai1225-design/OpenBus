# 14. FAQ

## 14.1 Connection

**Q: Connect does nothing or fails?**  
A: Confirm the driver is installed and the OS sees the device; check bottom **Output**; try another USB port / close apps that hold the device; ensure channels are enabled and baud is valid.

**Q: Connect succeeded but Trace stays empty?**  
A: Also **Start** measurement in **Flow**; ensure the filter does not drop every frame; confirm the bus (or simulator / peer) is actually transmitting.

**Q: CAN FD data-phase fields are grayed out?**  
A: Set **CAN mode** to CAN FD; Classic-only drivers (some SLCAN builds) keep FD disabled.

## 14.2 Trace / Graphic

**Q: Filter expression errors?**  
A: Open filter-bar help for syntax; an empty expression means no filter.

**Q: Graphic shows no waveforms?**  
A: Confirm a DBC is loaded, signals are added, measurement is running, and samples exist in the time window.

**Q: The list stutters?**  
A: Tighten filters; reduce concurrent Graphic curves; close unused measurement windows.

## 14.3 Projects and files

**Q: Opening an old project lost some windows?**  
A: Use **View > Reset Layout**; verify the project file is intact; recreate Trace / Graphic from the side bar.

**Q: Where are recorded files?**  
A: The Record page directory and prefix; use **Open folder** to reveal them in Explorer when available.

## 14.4 Extensions

**Q: Marketplace install failed?**  
A: Check network, disk permissions, and package verification; inspect the **Extensions** output page.

**Q: Plugin started but no window?**  
A: Some suites use a separate window — check the taskbar; or activate again from Running / Installed.

## 14.5 Support

- **Help > Report Issue**
- **Help > Documentation** / website (follow the menu links)
- Include: app version (About), OS version, device model, and a short **Output** / log excerpt

---

## Appendix A. Terms

| Term | Meaning |
|------|---------|
| CAN FD | CAN with Flexible Data-Rate |
| DBC | Message / signal definition database file |
| Trace | Frame list / trace view |
| Graphic | Signal waveform view |
| Flow | Measurement setup / start-stop |
| Panel | Shared bottom output area |

## Appendix B. Screenshot checklist

Full capture notes: [images/README.md](../user_manual/images/README.md).

---

← [Shortcuts](13-shortcuts.md) · [Manual home](README.md)
