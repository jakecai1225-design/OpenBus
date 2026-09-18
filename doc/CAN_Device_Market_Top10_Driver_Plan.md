# CAN / CAN FD Acquisition Device Market — Top 10 & Driver Plan

**Date**: 2026-09-18  
**Scope**: USB / PCIe / Mini-PCIe / virtual interfaces used for PC-based CAN & CAN FD capture (not full CANoe-class analyzer suites alone).  
**Method**: Combine (1) published market-report vendor rankings, (2) open-source ecosystem signals (python-can backends, GitHub stars, Linux SocketCAN), (3) China domestic volume brands, (4) openbus target users (lab, aftermarket, China auto/industrial).

> Exact public unit-shipment share for “CAN FD USB sticks” is not freely published. Rankings below are a **composite popularity index**, not audited market share.

---

## 1. Evidence sources (signals)

| Signal | What it measures | Notes |
|--------|------------------|--------|
| Analyzer / interface vendor revenue rankings (ReportPrime-style 2024 estimates) | Commercial footprint | Vector ≫ Kvaser ≈ PEAK ≫ Intrepid ≫ Softing |
| Global interface market reports | Brand presence | PEAK, Kvaser, Vector, NI, Intrepid, IXXAT/HMS, Lawicel, ZLG listed repeatedly |
| **python-can** (~1.5k★) backends | Developer mindshare | pcan, kvaser, vector, gs_usb, socketcan, ixxat, neovi, slcan, … |
| **candleLight_fw** (~900★) + CANable ecosystem | OSS hardware volume | gs_usb / SocketCAN native on Linux |
| **can-utils** / SocketCAN | Linux install base | De-facto standard on Linux/RPi |
| China vendors (ZLG / TOSUN / GCAN / BUSMUST) | Domestic unit volume | ZLG + TOSUN dominate China auto/lab; not always in Western reports |
| Toolchain lock-in | Sticky user base | Vector↔CANoe, TOSUN↔TSMaster, ZLG↔ZCANPRO/ZXDoc, PEAK↔PCAN-View |

---

## 2. Top 10 families (composite rank)

| Rank | Family / series | Vendor | Why high user volume | CAN FD | openbus today |
|------|-----------------|--------|----------------------|--------|---------------|
| **1** | **PCAN-USB / USB FD / USB Pro FD / miniPCIe** | PEAK-System | Mid-market leader; cheap; PCAN-Basic; python-can `pcan`; huge industrial+aftermarket | Yes (FD SKUs) | **Done** (`peak`) |
| **2** | **Leaf / USBcan Pro / U100 / PCIEcan v3 / Virtual** | Kvaser | #2 analyzer-hardware revenue; one CANlib for all SKUs; strong Linux | Yes | **Done** (`kvaser` v1.1) |
| **3** | **VN16xx / VN56xx / VX / XL Driver Library** | Vector | OEM / Tier1 default; CANoe ecosystem; highest commercial share | Yes | **Done** (`vector` v1.0) |
| **4** | **USBCAN / USBCANFD (100U/200U/400U/800U…)** | ZLG (致远) | China unit-volume leader; ZCANPRO/ZXDoc; already in openbus DNA | Yes | **Done** (`zlg`) |
| **5** | **candleLight / CANable / CANtact / clones (gs_usb)** | Open HW + many OEMs | Highest OSS stars among adapters; SocketCAN native; student/hacker volume | Partial (FD on newer HW) | **Done** (`candle`) |
| **6** | **SocketCAN (+ vcan) / can-utils stack** | Linux kernel | Every Linux box; CI / RPi / IPC; not a “dongle” but #1 Linux capture path | Yes (kernel) | **Done** (`socketcan` v1.0, Linux; Windows stub) |
| **7** | **ValueCAN 4 / neoVI FIRE/RED / RAD** | Intrepid | Strong NA automotive; python-can `neovi`; multi-protocol | Yes | **Missing** (P1) |
| **8** | **USB-to-CAN V2 / compact / FD (IXXAT)** | HMS / IXXAT | EU industrial standard; VCI SDK; python-can `ixxat` | Yes | **Done** (`ixxat` v1.0) |
| **9** | **TC10xx / TC101x Pro (同星 TOSUN)** | TOSUN | Fast-growing China auto tool chain with TSMaster; multi-ch FD | Yes | **Done** (`tongxing` v1.0) |
| **10** | **SLCAN / Lawicel CANUSB / USB2CAN ASCII** | Lawicel + clones | Long-tail; no vendor DLL; python-can `slcan`; DIY / legacy | Mostly classic | **Done** (`slcan`) |

### Honorable mentions (next wave)

| Family | Vendor | Reason to defer |
|--------|--------|-----------------|
| BUSMUST USB-CAN(FD) | BUSMUST | Already supported; China mid-tier |
| GCAN USBCAN-II FD | 广成 | China volume; similar to ZLG API style |
| Softing CAN interfaces | Softing | Diagnostics-heavy; smaller HW share |
| NI-XNET / NI-CAN | NI | LabView lock-in; lower openbus priority |
| ESD CAN-USB/2 | ESD | EU industrial; often SocketCAN on Linux |
| ETAS / Bosch | ETAS | OEM tooling; heavy SDK |

---

## 3. Gap analysis vs openbus

| Status | Drivers |
|--------|---------|
| **Shipped** | PEAK, Kvaser, ZLG, Candle/gs_usb, BUSMUST, SLCAN, Vector XL, TOSUN, IXXAT, SocketCAN, **Intrepid** |
| **Stub only** | — |
| **Not started** | GCAN |

Coverage of the Top 10 by family: **10/10 done** (SocketCAN Linux-only; Intrepid via libicsneo / A5).

---

## 4. Driver development plan

### Principles (same as Peak / Kvaser)

1. One `CanDeviceXxx` backend in `src/core/` + thin `drivers/<id>/` Qt plugin.  
2. Dynamic load vendor DLL; never hard-link proprietary SDK into `openbus_data` if avoidable.  
3. `DeviceInfo.deviceType` = open key (handle / channel / product id).  
4. Classic + ISO CAN FD; document non-ISO separately.  
5. Market asset + `driver.json` + smoke notes in `doc/`.

### Phase A — Close Top-10 gaps (priority order)

| Priority | Driver id | Target series | SDK | Effort | Milestone |
|----------|-----------|---------------|-----|--------|-----------|
| **A1** | `vector` | VN1610/1611/1630/1640, VN56xx via **vxlapi** | Vector XL Driver Library (Windows) | L | **Done** — enumerate + open + RX/TX classic/FD (`doc/Vector_Driver.md`) |
| **A2** | `tongxing` | TOSUN TC1011/1013/1014/1016/1017… | TOSUN libTSCAN / TSMaster SDK | M–L | **Done** — enumerate/open/RX/TX classic+FD (`doc/TongXing_Driver.md`) |
| **A3** | `ixxat` | USB-to-CAN V2 / FD | IXXAT VCI4 (`vcinpl2`) | M | **Done** — enumerate/open/RX/TX classic+FD (`doc/Ixxat_Driver.md`) |
| **A4** | `socketcan` | Linux `can0` / `vcan0` | SocketCAN (Linux only) | M | **Done** — enumerate/bind/RX/TX classic+FD (`doc/SocketCAN_Driver.md`); Windows stub |
| **A5** | `intrepid` | ValueCAN 4 / neoVI | Intrepid libicsneo (`icsneoc`) | L | **Done** — enumerate/open/RX/TX classic+FD (`doc/Intrepid_Driver.md`) |

### Phase B — Harden shipped Top-10 drivers

| Driver | Work |
|--------|------|
| `peak` | FD presets audit; multi-device; PCAN-View coexistence already partial |
| `kvaser` | Real-device soak; silent mode; filter ioctl; non-ISO FD flag |
| `zlg` | Keep USBCANFD matrix current; ZPS/net devices if demanded |
| `candle` | FD firmware variants; WinUSB vs libusb docs |
| `slcan` | FD extensions where firmware supports; bitrate table |

### Phase C — Long-tail / China volume

| Driver id | Series | Notes |
|-----------|--------|-------|
| `gcan` | 广成 USBCAN-II FD | Similar market to ZLG |
| `busmust` | Already done | Market polish only |
| `softing` | Softing CAN / EDIC | Lower priority |
| `ni` | NI-XNET | Lab only |

---

## 5. Suggested schedule (engineering)

| Sprint | Deliverable | Exit criteria |
|--------|-------------|---------------|
| **S1** | Plan + research doc (this file) | Reviewed |
| **S2** | `vector` skeleton: load `vxlapi64.dll`, enumerate ports | **Done** |
| **S3** | `vector` RX/TX classic + FD | **Done** (soak with VN16xx / Virtual when HW available) |
| **S4** | `tongxing` enumerate/open/RX/TX | **Done** (soak with TC101x when HW available) |
| **S5** | `ixxat` VCI4 classic + FD | **Done** (soak with USB-to-CAN when HW available) |
| **S6** | `socketcan` Linux backend | **Done** (soak with `vcan0` / `can0` on Linux) |
| **S7** | `intrepid` neoVI / ValueCAN | **Done** (soak with ValueCAN 4 when HW + icsneoc available) |
| **S8** | Market UI badges + `.odp` packs for new drivers | Install via plugin market |

Parallelism: S4 (TOSUN) can run beside S2–S3 if two owners.

---

## 6. Acceptance matrix (per new driver)

- [ ] `isAvailable()` false when SDK missing (no crash)  
- [ ] Enumerate shows human-readable names + FD capability  
- [ ] Open classic 500k; Trace RX ≥ 1k frame/s without UI freeze  
- [ ] Open FD arb 500k / data 2M; BRS frames round-trip  
- [ ] Close / reopen stable; multi-channel if hardware has it  
- [ ] `drivers/<id>/driver.json` + English-only code  
- [ ] Short `doc/<Vendor>_Driver.md` + smoke steps  

---

## 7. Risk notes

| Risk | Mitigation |
|------|------------|
| Vector XL license / redistribute | Document “install Vector Driver Setup”; do not ship `vxlapi64.dll` |
| TOSUN SDK NDA / headers | Prefer public libTSCAN; fallback community wrappers |
| Intrepid ICS API surface large | Start ValueCAN-only subset |
| SocketCAN Windows | Ship Linux-only plugin; keep Candle/SLCAN for Win OSS users |
| Market data uncertainty | Re-rank annually; track plugin install counts in openbus market |

---

## 8. Decision for openbus product

**Near-term:** Top-10 families covered (SocketCAN on Linux; Intrepid via libicsneo). Phase A complete.

Current Top-10 family coverage: **10/10** (A1–A5 done).

Already-strong China + OSS path (ZLG / PEAK / Kvaser / Candle / BUSMUST / SLCAN) stays in maintenance.

---

## References (non-exhaustive)

- ReportPrime / DataIntelo / ResearchIntelo CAN analyzer & CAN-FD-USB market summaries (2024–2033)  
- [python-can interfaces](https://python-can.readthedocs.io/en/main/interfaces.html)  
- [candleLight_fw](https://github.com/candle-usb/candleLight_fw)  
- [Kvaser CANlib](https://kvaser.com/canlib-sdk/) · PEAK PCAN-Basic · Vector XL Driver Library  
- Domestic: ZLG USBCANFD, TOSUN TC series + TSMaster, GCAN USBCAN-II FD  
- openbus: `drivers/*/driver.json`, `doc/Kvaser_Driver.md`, `DriverRegistry`
