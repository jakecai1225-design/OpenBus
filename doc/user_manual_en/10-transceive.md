# 10. Transceive

The **Transceive** activity aggregates send / playback / record / offline pages, usually switched from the side bar or tabs.

## 10.1 Send

Edit the send list, periodic send, and single-shot send.

### Layout

- Toolbar: **Send All** / **Stop All** / **Clear List**
- Upper table: send list (On, ID, Name, DLC, Data, Period, Count, Status, Actions)
- Lower area: **Edit frame** — ID / DLC / Data, Period, Count, plus **Import from DBC**, **Add to List**, **Send Once**

![Send page](images/10-send.png)

### Recommended steps

1. Device Connected; Flow Started if you need echo into Trace.
2. Enter ID / Data manually, or **Import from DBC**.
3. **Add to List**, enable **On**.
4. Use row Send, or top **Send All**.
5. **Stop All** or row Stop to end cyclic send.

| Status | Meaning |
|--------|---------|
| Ready | Ready |
| Sending | Sending |
| Stopped | Stopped |

## 10.2 Playback

Replay a log onto the bus or analysis path on a timeline.

1. **Add files** or drop `.blf` / `.asc` / `.csv`, etc.
2. Select a row; use **Play** / **Pause** / **Stop**.
3. Adjust **Speed**, **Loop**, **Auto-scroll during playback**.
4. In **Playback settings**, limit Channel / Direction / Protocol / Filter as needed.

![Playback page](images/10-playback.png)

## 10.3 Record

Write bus data to files.

1. Set **directory**, **prefix**, and **format** (BLF / ASC / CSV).
2. Optional: split by size / time, ring overwrite, buffer size.
3. **Record filter**: all / Rx only / Tx only / CAN FD only, plus ID lists.
4. Optional **Trigger Recording** (expression trigger, pre/post buffers).
5. **Start** / **Pause** / **Stop** recording.

![Record page](images/10-record.png)

## 10.4 Offline Analysis

Batch-add analysis files, parse frame count / duration / size, then continue analysis.

1. **Add files** or drag and drop.
2. The list shows parse progress and summaries.
3. Open Trace / continue with the buttons available in the current build.

![Offline analysis](images/10-offline.png)

---

← [Database](09-database.md) · [Manual home](README.md) · Next: [Extensions](11-extensions.md) →
