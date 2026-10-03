# Soak checklist — CANopen Suite Top3

**Duration:** ≥2 hours connected (or split sessions totaling 2h)  
**Goal:** No crash; SDO/NMT/Trace/HB remain usable after long run.

## Prep

1. Launch OpenBus; activate **CANopen Suite** (source `plugins/`).
2. Open or New EDS → Profiles if needed → Check → Save → Apply → Live OD.
3. Note Node-ID; ensure bus hardware or loopback available.

## Loop (repeat)

| Step | Action | Expect |
|------|--------|--------|
| 1 | Scan → select node → Apply | Live OD opens; Node-ID set |
| 2 | Live OD Read / Read-all | Values fill; abort shows text if denied |
| 3 | NMT Pre-op → Start | HB label updates; health row ages |
| 4 | Trace open; traffic flowing | COB class + SDO/EMCY decode |
| 5 | Live PDO From EDS / Read node | Slots listed; Live column updates on TPDO |
| 6 | Drive Read status (+ optional CW with confirm) | Statusword state text |
| 7 | Optional LSS inquire (lab only) | Confirm dialogs; no silent write |
| 8 | Codegen after Check OK | OD.h/OD.c save |

## Drop / recover

1. Unplug / stop bus mid SDO → expect timeout note, UI stays up.
2. Reconnect → Scan / Read again without restarting OpenBus.

## Pass criteria

- [ ] No uncaught exception / suite deactivate
- [ ] OUTPUT shows abort / HB timeout / EMCY when induced
- [ ] Memory/UI remains responsive at end of 2h

Sign-off: ________  Date: ________
