# 1. Introduction and installation

## 1.1 What OpenBus is

**OpenBus** is a desktop **CAN / CAN FD bus analysis** application for automotive electronics and industrial field work. It brings project management, device connection, measurement setup (Flow), live frame lists (Trace), signal waveforms (Graphic), DBC decode, transmit / record / playback, and an extensible driver / plugin marketplace into one VS Code–style workbench.

Typical uses:

- Capture and filter CAN frames on a vehicle or test bench in real time
- Decode signals with DBC and plot them in Graphic
- Send periodic or list-based test frames
- Record BLF / ASC / CSV, or replay logs for offline analysis
- Install hardware drivers and domain suites (for example diagnostics) from the marketplace

## 1.2 System requirements

| Item | Recommended |
|------|-------------|
| OS | Windows 10 / 11, 64-bit |
| RAM | ≥ 8 GB (16 GB recommended for heavy Trace + Graphic) |
| Display | 1920×1080 or higher |
| Hardware | Supported CAN adapter (ZLG, PEAK, Kvaser, Candle, SLCAN, BUSMUST, and others — see marketplace drivers) |
| Optional | Vendor runtime / DLL (some devices need the vendor stack first) |

## 1.3 Install and start

1. Get the release package (**installer** `openbus-*-windows-x64-setup.exe`, or a portable zip).
2. **Installer**: the first dialog is the wizard language (English / 简体中文 / Deutsch / …). That choice is stored as the UI language for the first launch (you can still change it later under **Settings → General → Language**).
3. Finish setup, or extract the portable zip to a path without awkward permission issues.
4. Start **OpenBus**. First launch usually opens the **Welcome** page.

![First-launch main window](images/01-welcome.png)

> **Screenshot tip**: Capture the maximized main window with the menu bar, activity bar, Welcome content, and bottom Panel.

## 1.4 Uninstall

- **Installer build**: remove the app from Windows Settings → Apps.
- **Portable build**: quit the app and delete the extract folder; user settings usually live under the per-user application data directory (see the release notes for the exact path).

## 1.5 Getting help

In the app:

- **Help > Welcome** — welcome page and onboarding tips
- **Help > Keyboard Shortcuts** — shortcuts
- **Help > Documentation** / **Website** / **Report Issue** — docs and feedback

---

← [Manual home](README.md) · Next: [Getting started](02-getting-started.md) →
