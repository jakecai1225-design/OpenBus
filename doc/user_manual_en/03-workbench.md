# 3. Workbench overview

OpenBus follows a **VS Code–style** layout: the activity bar switches workspaces, the center holds editor tabs, the bottom hosts a shared Panel, and the top bar holds menus and the command center.

## 3.1 Layout regions

```text
┌─ MenuBar / Command Center / layout toggles / window buttons ─┐
├─ ActivityBar ┬─ Side Bar ─┬─ Editor Tabs (Trace / Flow / …) ─┤
│  icon strip  │  tree      │                                 │
│              │            ├─────────────────────────────────┤
│              │            │ Bottom Panel (Terminal/Output/…) │
└──────────────┴────────────┴─────────────────────────────────┘
└─ Status Bar ────────────────────────────────────────────────┘
```

![Annotated workbench](images/03-workbench-overview.png)

| Region | Role |
|--------|------|
| **Activity Bar** | Switch among Project / Flow / Device / Trace / Graphic / Database / Transceive / Extensions |
| **Side Bar** | Trees, lists, and shortcuts for the current workspace |
| **Editor** | Main area: tabs, splits, floating windows |
| **Bottom Panel** | Terminal / Output / Problems / Extensions output (visible by default) |
| **Right Panel** | Inspector / Bookmarks / Watch (hidden by default) |
| **Status Bar** | Ready, connection, frame count, selection, time |

## 3.2 Layout toggles

Title-bar buttons (VS Code–like):

| Action | Shortcut | Notes |
|--------|----------|-------|
| Toggle left bar | `Ctrl+B` | Primary Side Bar |
| Toggle bottom panel | `Ctrl+J` | Panel |
| Toggle right bar | (button) | Secondary Side Bar |

Menus: **View > Primary Side Bar / Panel / Secondary Side Bar**, and **View > Reset Layout**.

## 3.3 Menu bar summary

| Menu | Common items |
|------|----------------|
| **File** | Open File…, Open / Save Project, Import Log File…, Exit |
| **View** | Side bars / panel, Welcome, Reset Layout, Command Center |
| **Tools** | Data Window, I/O Graph, Watcher, Color Rules… |
| **Help** | Welcome, About, docs, shortcuts, license |

## 3.4 Command Center and Command Palette

- Search box in the title bar: **Command Center** (`Ctrl+P`)
- Command mode: `Ctrl+Shift+P`, type `>` to run commands or open views

![Command palette](images/03-command-palette.png)

## 3.5 Bottom Panel

Open by default; tabs include:

- **Terminal** — embedded terminal
- **Output** — app and extension logs
- **Problems** — problem list
- **Extensions** — extension output

Clear / more / close actions sit in the panel’s top-right corner.

## 3.6 Reading the status bar

- Left: primary status (for example Ready) and connection summary
- Right: active tab, frame count, selection, time

Use the status bar for quiet cues; detailed logs live in **Output**.

---

← [Getting started](02-getting-started.md) · [Manual home](README.md) · Next: [Project](04-project.md) →
