# 9. Database (DBC)

## 9.1 Purpose

**Database** manages decode databases (mainly **DBC** today): load files, browse nodes / messages / signals, and inspect attributes and value tables. Trace decode and Graphic signals depend on definitions loaded here.

## 9.2 Open the Database workspace

1. Click **Database** in the activity bar.
2. The **Databases** side bar lists loaded files.
3. Double-click or open a file to show DBC details (tree on the left, tables on the right).

![DBC detail](images/09-dbc-detail.png)

## 9.3 Load a DBC

Common entry points:

- Add / open `.dbc` from the side bar
- **File > Open File…** and choose `.dbc`
- Project-stored database references (restored when the project opens)

## 9.4 Browse structure

The left tree usually includes:

- Messages and CAN IDs
- Signals
- Network nodes
- Value tables, etc.

The right pane shows, based on selection:

- Message signal tables (start bit, length, endianness, factor, offset, unit, …)
- Signal attributes
- Node TX / RX lists

Use the top search box to filter tree nodes quickly.

## 9.5 Relation to send / observe

- **Send (Transceive)** can import messages from DBC into the send list.
- **Watcher** can multi-select signals from loaded DBCs.
- **Graphic** uses signal definitions for physical curves.

## 9.6 Notes

- Encoding, extended frames, and multi-file merge rules follow the current parser.
- After editing an external DBC, reload it in the app before expecting changes.

---

← [Graphic](08-graphic.md) · [Manual home](README.md) · Next: [Transceive](10-transceive.md) →
