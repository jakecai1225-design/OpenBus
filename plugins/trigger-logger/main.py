# -*- coding: utf-8 -*-
"""trigger-logger — condition-triggered capture (CANalyzer Trigger style).

Multi-row OR triggers (ID+mask or DBC signal compare), pre ring buffer,
post-capture by frame count or time (ms), ASC + CSV export.
"""

from __future__ import annotations

import os
import time
from collections import deque

from PyQt6.QtCore import QTimer
from PyQt6.QtGui import QColor
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QLineEdit,
    QTreeWidget, QTreeWidgetItem, QTextEdit, QFileDialog, QMessageBox,
    QHeaderView, QGroupBox, QFormLayout, QSpinBox, QCheckBox, QComboBox,
    QTableWidget, QAbstractItemView,
)

import sin
from _shared import dbcparse, dbc_picker, plugin_shell, state_store

PLUGIN_ID = "trigger-logger"

OPS = ("==", "!=", ">", "<", ">=", "<=")

_pre_buf = deque(maxlen=1000)
_post_frames = []
_frozen = []          # completed capture (pre+post) for display/export
_events = []
_triggers = []        # active conditions while armed
_state = {
    "armed": False,
    "mode_once": True,
    "triggered": False,
    "post_mode": "time",   # "frames" | "time"
    "post_target_frames": 100,
    "post_target_ms": 500,
    "post_count": 0,
    "trigger_ts": 0.0,
    "pre_depth": 1000,
}
_dbc = None
_dbc_path = ""
_running = True


def _parse_bytes(text):
    text = (text or "").strip()
    if not text:
        return None
    try:
        cleaned = text.replace(" ", "").replace(",", "")
        if len(cleaned) % 2:
            cleaned = "0" + cleaned
        return bytes.fromhex(cleaned)
    except ValueError:
        return b""


def _cmp(op, left, right):
    try:
        if op == "==":
            return left == right
        if op == "!=":
            return left != right
        if op == ">":
            return left > right
        if op == "<":
            return left < right
        if op == ">=":
            return left >= right
        if op == "<=":
            return left <= right
    except TypeError:
        return False
    return False


def _check_triggers(frame):
    if not _triggers:
        return True
    for cond in _triggers:
        kind = cond.get("kind", "id_mask")
        if kind == "id_mask":
            if frame.id != cond["id"]:
                continue
            mask = cond.get("mask")
            value = cond.get("value")
            if not mask:
                return True
            data = frame.data or b""
            ok = True
            for i, mask_byte in enumerate(mask):
                if i >= len(data):
                    ok = False
                    break
                want = value[i] if value and i < len(value) else 0
                if (data[i] & mask_byte) != (want & mask_byte):
                    ok = False
                    break
            if ok:
                return True
        elif kind == "signal":
            msg = cond.get("msg")
            sig = cond.get("sig")
            if msg is None or sig is None:
                continue
            if frame.id != msg.can_id:
                continue
            phys = dbcparse.signal_phys(sig, frame.data or b"")
            if phys is None:
                continue
            if _cmp(cond.get("op", "=="), phys, cond.get("cmp_value", 0.0)):
                return True
    return False


def _on_frame(frame):
    if not _running or not _state["armed"]:
        return
    rec = (
        time.time(),
        frame.id,
        bool(getattr(frame, "extended", False)),
        getattr(frame, "direction", "Rx") or "Rx",
        getattr(frame, "dlc", len(frame.data or b"")),
        bytes(frame.data or b""),
    )
    if not _state["triggered"]:
        _pre_buf.append(rec)
        if _check_triggers(frame):
            _state["triggered"] = True
            _state["post_count"] = 0
            _state["trigger_ts"] = rec[0]
            _events.append((
                time.time(), "TRIGGER",
                "ID 0x%X matched" % frame.id))
            _post_frames.append(rec)
    else:
        _post_frames.append(rec)
        _state["post_count"] += 1
        done = False
        if _state["post_mode"] == "frames":
            done = _state["post_count"] >= _state["post_target_frames"]
        else:
            elapsed_ms = (rec[0] - _state["trigger_ts"]) * 1000.0
            done = elapsed_ms >= _state["post_target_ms"]
        if done:
            _freeze_capture()
            _events.append((
                time.time(), "DONE",
                "Post window closed (%d frames, mode=%s)"
                % (_state["post_count"], _state["post_mode"])))
            if _state["mode_once"]:
                _state["armed"] = False
                _state["triggered"] = False
            else:
                _pre_buf.clear()
                del _post_frames[:]
                _state["triggered"] = False


def _freeze_capture():
    global _frozen
    # Exclude the trigger frame from double-count: first post is the trigger
    pre = list(_pre_buf)
    post = list(_post_frames)
    # If trigger frame was also pushed into pre before fire, drop last pre if same ts
    if pre and post and pre[-1][0] == post[0][0] and pre[-1][1] == post[0][1]:
        pre = pre[:-1]
    _frozen = pre + post


def activate(context):
    global _running, _pre_buf, _dbc, _dbc_path, _triggers, _frozen
    _running = True
    _pre_buf = deque(maxlen=1000)
    del _post_frames[:]
    _frozen = []
    del _events[:]
    _triggers = []
    _state.update({
        "armed": False, "triggered": False, "post_count": 0, "trigger_ts": 0.0,
    })

    win = sin.ui.create_window("Trigger Logger")
    win.resize(1040, 700)
    plugin_shell.attach_status_bar(win, "Disarmed")

    central = QWidget()
    win.setCentralWidget(central)
    layout = QVBoxLayout(central)

    # --- DBC ---
    dbc_row = QHBoxLayout()
    dbc_label = QLabel("DBC: (none)")
    dbc_label.setStyleSheet("color:#78909c;")
    dbc_btn = QPushButton("Load DBC…")
    dbc_row.addWidget(dbc_label, 1)
    dbc_row.addWidget(dbc_btn)
    layout.addLayout(dbc_row)

    layout.addWidget(plugin_shell.help_label(
        "Triggers are OR'd: any matching row fires capture. Use ID+mask rows "
        "and/or DBC signal compare rows after loading a DBC. Post-capture can "
        "be by frame count or by time (ms)."))

    # --- Trigger table ---
    trig_box = QGroupBox("Trigger list (OR)")
    trig_l = QVBoxLayout(trig_box)
    trig_table = QTableWidget(0, 6)
    trig_table.setHorizontalHeaderLabels(
        ["Kind", "ID / Message", "Mask / Signal", "Op", "Value", "Enabled"])
    trig_table.horizontalHeader().setSectionResizeMode(
        QHeaderView.ResizeMode.Stretch)
    trig_table.setSelectionBehavior(
        QAbstractItemView.SelectionBehavior.SelectRows)
    trig_table.setMaximumHeight(180)
    trig_l.addWidget(trig_table)
    trig_btns = QHBoxLayout()
    add_id_btn = QPushButton("Add ID+mask")
    add_sig_btn = QPushButton("Add signal")
    rm_trig_btn = QPushButton("Remove row")
    trig_btns.addWidget(add_id_btn)
    trig_btns.addWidget(add_sig_btn)
    trig_btns.addWidget(rm_trig_btn)
    trig_btns.addStretch(1)
    trig_l.addLayout(trig_btns)
    layout.addWidget(trig_box)

    # --- Capture params ---
    cfg = QGroupBox("Capture window")
    cfg_l = QFormLayout(cfg)
    pre_spin = QSpinBox()
    pre_spin.setRange(10, 50000)
    pre_spin.setValue(1000)
    pre_spin.setSuffix(" frames")
    post_mode = QComboBox()
    post_mode.addItem("Time (ms)", "time")
    post_mode.addItem("Frame count", "frames")
    post_frames_spin = QSpinBox()
    post_frames_spin.setRange(1, 100000)
    post_frames_spin.setValue(100)
    post_frames_spin.setSuffix(" frames")
    post_ms_spin = QSpinBox()
    post_ms_spin.setRange(1, 600000)
    post_ms_spin.setValue(500)
    post_ms_spin.setSuffix(" ms")
    once_chk = QCheckBox("Single-shot (disarm after one capture; unchecked = repeat)")
    once_chk.setChecked(True)
    cfg_l.addRow("Pre-trigger buffer:", pre_spin)
    cfg_l.addRow("Post mode:", post_mode)
    cfg_l.addRow("Post frames:", post_frames_spin)
    cfg_l.addRow("Post time:", post_ms_spin)
    cfg_l.addRow(once_chk)
    layout.addWidget(cfg)

    def _sync_post_spins():
        is_time = post_mode.currentData() == "time"
        post_ms_spin.setEnabled(is_time)
        post_frames_spin.setEnabled(not is_time)

    post_mode.currentIndexChanged.connect(_sync_post_spins)
    _sync_post_spins()

    btns = QHBoxLayout()
    arm_btn = QPushButton("Arm")
    disarm_btn = QPushButton("Disarm")
    export_csv_btn = QPushButton("Export CSV")
    export_asc_btn = QPushButton("Export ASC")
    save_btn = QPushButton("Save config")
    clear_btn = QPushButton("Clear")
    for b in (arm_btn, disarm_btn):
        btns.addWidget(b)
    btns.addStretch(1)
    for b in (export_csv_btn, export_asc_btn, save_btn, clear_btn):
        btns.addWidget(b)
    layout.addLayout(btns)

    status = QLabel("Disarmed")
    status.setStyleSheet("font-weight:bold;")
    layout.addWidget(status)

    empty = plugin_shell.empty_state_label(
        "No frozen capture yet. Arm triggers, wait for a match, then export.")
    layout.addWidget(empty)

    tree = QTreeWidget()
    tree.setHeaderLabels(
        ["#", "Time", "ID", "Dir", "DLC", "Data", "Rel to trigger"])
    tree.setRootIsDecorated(False)
    tree.setAlternatingRowColors(True)
    tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    layout.addWidget(tree, 1)
    tree.hide()

    event_view = QTextEdit()
    event_view.setReadOnly(True)
    event_view.setMaximumHeight(100)
    layout.addWidget(event_view)

    # --- Signal picker helpers ---
    _msg_list = []  # [(can_id, name, msg)]

    def _refresh_msg_list():
        nonlocal _msg_list
        _msg_list = []
        if _dbc is None:
            return
        for mid in sorted(_dbc.messages.keys()):
            m = _dbc.messages[mid]
            _msg_list.append((mid, m.name, m))

    def _set_dbc_label():
        if _dbc_path:
            dbc_label.setText(
                "DBC: %s (%d messages)"
                % (os.path.basename(_dbc_path), len(_dbc.messages) if _dbc else 0))
            dbc_label.setStyleSheet("color:#2e7d32;")
        else:
            dbc_label.setText("DBC: (none)")
            dbc_label.setStyleSheet("color:#78909c;")

    def _add_row(kind="id_mask", data=None):
        data = data or {}
        r = trig_table.rowCount()
        trig_table.insertRow(r)

        kind_combo = QComboBox()
        kind_combo.addItem("ID+mask", "id_mask")
        kind_combo.addItem("Signal", "signal")
        kind_combo.setCurrentIndex(0 if kind == "id_mask" else 1)
        trig_table.setCellWidget(r, 0, kind_combo)

        id_edit = QLineEdit(str(data.get("id_text", "0x123")))
        trig_table.setCellWidget(r, 1, id_edit)

        mask_or_sig = QLineEdit(str(data.get("mask_or_sig", "")))
        if kind == "id_mask":
            mask_or_sig.setPlaceholderText("Mask hex e.g. FF 00 FF (empty=any data)")
        else:
            mask_or_sig.setPlaceholderText("Signal name")
        trig_table.setCellWidget(r, 2, mask_or_sig)

        op_combo = QComboBox()
        for op in OPS:
            op_combo.addItem(op)
        op_combo.setCurrentText(data.get("op", "=="))
        op_combo.setEnabled(kind == "signal")
        trig_table.setCellWidget(r, 3, op_combo)

        val_edit = QLineEdit(str(data.get("value", "")))
        if kind == "id_mask":
            val_edit.setPlaceholderText("Compare data hex (with mask)")
        else:
            val_edit.setPlaceholderText("Compare value")
        trig_table.setCellWidget(r, 4, val_edit)

        en_chk = QCheckBox()
        en_chk.setChecked(data.get("enabled", True))
        en_chk.setStyleSheet("margin-left:18px;")
        trig_table.setCellWidget(r, 5, en_chk)

        def _on_kind_changed(_idx, row=r, kc=kind_combo, oc=op_combo, ms=mask_or_sig, ve=val_edit):
            is_sig = kc.currentData() == "signal"
            oc.setEnabled(is_sig)
            if is_sig:
                ms.setPlaceholderText("Signal name (e.g. EngineSpeed)")
                ve.setPlaceholderText("Compare value")
                # Put message picker text into col1 if empty-ish
                if id_edit.text() in ("0x123", ""):
                    if _msg_list:
                        mid, name, _m = _msg_list[0]
                        id_edit.setText("0x%X %s" % (mid, name))
            else:
                ms.setPlaceholderText("Mask hex e.g. FF 00 FF (empty=any data)")
                ve.setPlaceholderText("Compare data hex (with mask)")

        kind_combo.currentIndexChanged.connect(_on_kind_changed)
        if kind == "signal":
            _on_kind_changed(1)

    def _row_data(r):
        kind_w = trig_table.cellWidget(r, 0)
        id_w = trig_table.cellWidget(r, 1)
        ms_w = trig_table.cellWidget(r, 2)
        op_w = trig_table.cellWidget(r, 3)
        val_w = trig_table.cellWidget(r, 4)
        en_w = trig_table.cellWidget(r, 5)
        if not kind_w:
            return None
        return {
            "kind": kind_w.currentData(),
            "id_text": id_w.text().strip() if id_w else "",
            "mask_or_sig": ms_w.text().strip() if ms_w else "",
            "op": op_w.currentText() if op_w else "==",
            "value": val_w.text().strip() if val_w else "",
            "enabled": en_w.isChecked() if en_w else True,
        }

    def _resolve_msg(id_text):
        """Parse '0x123 Name' or plain id into Message if DBC known."""
        text = id_text.strip()
        if not text:
            return None, None
        # Prefer exact can_id parse from leading token
        token = text.split()[0]
        try:
            cid = int(token, 0)
        except ValueError:
            cid = None
        if _dbc is not None and cid is not None and cid in _dbc.messages:
            return cid, _dbc.messages[cid]
        if _dbc is not None:
            for mid, name, msg in _msg_list:
                if name == text or text.endswith(name):
                    return mid, msg
        return cid, None

    def _build_triggers_from_table():
        out = []
        for r in range(trig_table.rowCount()):
            row = _row_data(r)
            if not row or not row["enabled"]:
                continue
            if row["kind"] == "id_mask":
                try:
                    cid = int(row["id_text"].split()[0], 0)
                except (ValueError, IndexError):
                    raise ValueError("Row %d: invalid ID" % (r + 1))
                mask = _parse_bytes(row["mask_or_sig"])
                value = _parse_bytes(row["value"])
                if mask is None or all(b == 0 for b in (mask or b"")):
                    out.append({"kind": "id_mask", "id": cid, "mask": None, "value": None})
                else:
                    if value is None or len(value) < len(mask):
                        value = (value or b"").ljust(len(mask), b"\x00")
                    out.append({
                        "kind": "id_mask", "id": cid, "mask": mask, "value": value,
                    })
            else:
                cid, msg = _resolve_msg(row["id_text"])
                if msg is None:
                    raise ValueError(
                        "Row %d: load DBC and pick a message for signal trigger" % (r + 1))
                sig_name = row["mask_or_sig"]
                sig = msg.signal(sig_name) if sig_name else None
                if sig is None:
                    raise ValueError("Row %d: unknown signal '%s'" % (r + 1, sig_name))
                try:
                    cmp_v = float(row["value"])
                except ValueError:
                    raise ValueError("Row %d: signal compare needs a numeric value" % (r + 1))
                out.append({
                    "kind": "signal",
                    "msg": msg,
                    "sig": sig,
                    "op": row["op"],
                    "cmp_value": cmp_v,
                    "msg_id": msg.can_id,
                    "sig_name": sig.name,
                })
        return out

    def _serialize_config():
        rows = []
        for r in range(trig_table.rowCount()):
            row = _row_data(r)
            if row:
                rows.append(row)
        return {
            "triggers": rows,
            "pre_depth": pre_spin.value(),
            "post_mode": post_mode.currentData(),
            "post_frames": post_frames_spin.value(),
            "post_ms": post_ms_spin.value(),
            "once": once_chk.isChecked(),
            "dbc_path": _dbc_path,
        }

    def _apply_config(cfg_data):
        global _dbc, _dbc_path
        if not cfg_data:
            return
        path = cfg_data.get("dbc_path") or ""
        if path and os.path.isfile(path):
            db = dbcparse.parse_file(path)
            if db.messages:
                _dbc = db
                _dbc_path = path
                _refresh_msg_list()
                _set_dbc_label()
        pre_spin.setValue(int(cfg_data.get("pre_depth", 1000)))
        mode = cfg_data.get("post_mode", "time")
        idx = post_mode.findData(mode)
        if idx >= 0:
            post_mode.setCurrentIndex(idx)
        post_frames_spin.setValue(int(cfg_data.get("post_frames", 100)))
        post_ms_spin.setValue(int(cfg_data.get("post_ms", 500)))
        once_chk.setChecked(bool(cfg_data.get("once", True)))
        trig_table.setRowCount(0)
        for row in cfg_data.get("triggers") or []:
            _add_row(row.get("kind", "id_mask"), row)
        if trig_table.rowCount() == 0:
            _add_row("id_mask")

    def _persist():
        path = state_store.save_state(PLUGIN_ID, _serialize_config(), "config.json")
        plugin_shell.set_status(win, "Saved %s" % path, 4000)
        return path

    def on_load_dbc():
        global _dbc, _dbc_path
        path = dbc_picker.pick_dbc(win, "Load DBC for signal triggers")
        if not path:
            return
        db = dbcparse.parse_file(path)
        if not db.messages:
            QMessageBox.warning(win, "DBC", "No messages in file")
            return
        _dbc = db
        _dbc_path = path
        _refresh_msg_list()
        _set_dbc_label()
        _persist()
        plugin_shell.set_status(win, "DBC loaded", 3000)

    def on_arm():
        global _pre_buf, _triggers, _frozen
        try:
            _triggers = _build_triggers_from_table()
        except ValueError as e:
            QMessageBox.warning(win, "Triggers", str(e))
            return
        _pre_buf = deque(maxlen=pre_spin.value())
        _state["pre_depth"] = pre_spin.value()
        _state["post_mode"] = post_mode.currentData() or "time"
        _state["post_target_frames"] = post_frames_spin.value()
        _state["post_target_ms"] = post_ms_spin.value()
        _state["mode_once"] = once_chk.isChecked()
        _state["armed"] = True
        _state["triggered"] = False
        _state["post_count"] = 0
        del _post_frames[:]
        arm_btn.setEnabled(False)
        disarm_btn.setEnabled(True)
        n = len(_triggers)
        desc = "%d trigger(s)" % n if n else "any frame"
        post_desc = (
            "%d ms" % _state["post_target_ms"]
            if _state["post_mode"] == "time"
            else "%d frames" % _state["post_target_frames"])
        status.setText(
            "Armed: %s · pre %d · post %s · %s"
            % (desc, pre_spin.value(), post_desc,
               "single-shot" if once_chk.isChecked() else "repeat"))
        plugin_shell.set_status(win, "Armed")
        _events.append((time.time(), "ARM", desc))
        _persist()

    def on_disarm():
        _state["armed"] = False
        _state["triggered"] = False
        arm_btn.setEnabled(True)
        disarm_btn.setEnabled(False)
        status.setText("Disarmed")
        plugin_shell.set_status(win, "Disarmed")
        _events.append((time.time(), "DISARM", ""))

    def _all_frames():
        if _frozen:
            return list(_frozen)
        if _post_frames:
            _freeze_capture()
            return list(_frozen)
        return list(_pre_buf)

    def _show_frames(frames):
        if not frames:
            tree.hide()
            empty.show()
            return
        empty.hide()
        tree.show()
        tree.clear()
        # Trigger time ≈ first post / middle of capture
        trig_time = _state.get("trigger_ts") or frames[0][0]
        for i, (ts, cid, ext, direction, dlc, data) in enumerate(frames[-500:]):
            rel = (ts - trig_time) * 1000.0
            item = QTreeWidgetItem([
                str(i),
                time.strftime("%H:%M:%S.%f", time.localtime(ts))[:-3],
                ("0x%X" % cid) + ("x" if ext else ""),
                str(direction),
                str(dlc),
                " ".join("%02X" % b for b in data),
                "%+.1f ms" % rel,
            ])
            if rel >= 0:
                item.setBackground(6, QColor("#e3f2fd"))
            else:
                item.setBackground(6, QColor("#fff8e1"))
            tree.addTopLevelItem(item)

    def refresh():
        if _events:
            event_view.setPlainText("\n".join(
                "[%s] %s %s" % (
                    time.strftime("%H:%M:%S", time.localtime(ts)), k, t)
                for ts, k, t in _events[-80:]))
            sb = event_view.verticalScrollBar()
            sb.setValue(sb.maximum())
        if _state["armed"]:
            extra = ""
            if _state["triggered"]:
                if _state["post_mode"] == "time":
                    elapsed = (time.time() - _state["trigger_ts"]) * 1000.0
                    extra = " · triggered, post %.0f/%d ms" % (
                        elapsed, _state["post_target_ms"])
                else:
                    extra = " · triggered, post %d/%d frames" % (
                        _state["post_count"], _state["post_target_frames"])
            status.setText(
                "Armed · pre %d/%d%s"
                % (len(_pre_buf), _state["pre_depth"], extra))
        if _frozen:
            _show_frames(_frozen)
        elif (not _state["armed"]) and _post_frames:
            _show_frames(list(_pre_buf) + _post_frames)

    def on_export_csv():
        frames = _all_frames()
        if not frames:
            QMessageBox.information(win, "Export", "No frozen data — arm and wait for a trigger")
            return
        t0 = frames[0][0]
        rows = []
        for ts, cid, ext, direction, dlc, data in frames:
            rows.append([
                "%.6f" % (ts - t0),
                "0x%X" % cid,
                "1" if ext else "0",
                direction,
                dlc,
                " ".join("%02X" % b for b in data),
            ])
        path = plugin_shell.export_csv(
            win,
            ["RelTime_s", "ID", "Extended", "Direction", "DLC", "Data"],
            rows,
            "trigger_capture.csv",
        )
        if path:
            plugin_shell.set_status(win, "Exported CSV %s" % path, 4000)

    def on_export_asc():
        frames = _all_frames()
        if not frames:
            QMessageBox.information(win, "Export", "No frozen data — arm and wait for a trigger")
            return
        path, _ = QFileDialog.getSaveFileName(
            win, "Export ASC", "trigger_capture.asc",
            "ASC (*.asc);;All files (*)")
        if not path:
            return
        try:
            t0 = frames[0][0]
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write("date %s\n" % time.strftime("%a %b %d %H:%M:%S %Y"))
                f.write("base hex  timestamps absolute\n")
                f.write("no internal events logged\n")
                for ts, cid, ext, direction, dlc, data in frames:
                    rel = ts - t0
                    id_str = ("%X" % (cid & (0x1FFFFFFF if ext else 0x7FF))).upper()
                    if ext:
                        id_str = id_str.zfill(8) + "x"
                    else:
                        id_str = id_str.zfill(3)
                    dir_s = "Tx" if str(direction).upper().startswith("T") else "Rx"
                    data_s = " ".join("%02X" % b for b in data)
                    # Vector-ish: timestamp channel ID Rx d DLC data
                    f.write("%.6f 1  %s  %s  d %d %s\n"
                            % (rel, id_str, dir_s, int(dlc), data_s))
            plugin_shell.set_status(win, "Exported ASC %s" % path, 4000)
            _events.append((time.time(), "EXPORT", path))
        except OSError as e:
            QMessageBox.warning(win, "Export failed", str(e))

    def on_clear():
        global _frozen
        _pre_buf.clear()
        del _post_frames[:]
        _frozen = []
        del _events[:]
        tree.clear()
        tree.hide()
        empty.show()
        plugin_shell.set_status(win, "Cleared")

    def on_add_id():
        _add_row("id_mask")

    def on_add_sig():
        if _dbc is None:
            QMessageBox.information(win, "DBC", "Load a DBC first for signal triggers")
            return
        # Prefill first message
        data = {}
        if _msg_list:
            mid, name, msg = _msg_list[0]
            data["id_text"] = "0x%X %s" % (mid, name)
            if msg.signals:
                data["mask_or_sig"] = msg.signals[0].name
                data["value"] = "0"
        _add_row("signal", data)

    def on_rm_row():
        r = trig_table.currentRow()
        if r >= 0:
            trig_table.removeRow(r)

    context.on_frame(_on_frame)
    context.register_command(
        "triggerLogger.open",
        plugin_shell.bind_raise(win),
        "Capture: Trigger Logger",
    )

    dbc_btn.clicked.connect(on_load_dbc)
    add_id_btn.clicked.connect(on_add_id)
    add_sig_btn.clicked.connect(on_add_sig)
    rm_trig_btn.clicked.connect(on_rm_row)
    arm_btn.clicked.connect(on_arm)
    disarm_btn.clicked.connect(on_disarm)
    export_csv_btn.clicked.connect(on_export_csv)
    export_asc_btn.clicked.connect(on_export_asc)
    save_btn.clicked.connect(_persist)
    clear_btn.clicked.connect(on_clear)
    disarm_btn.setEnabled(False)

    plugin_shell.bind_shortcut(win, "Ctrl+S", _persist)
    plugin_shell.bind_shortcut(win, "Ctrl+E", on_export_csv)

    timer = QTimer(win)
    timer.timeout.connect(refresh)
    timer.start(250)

    saved = state_store.load_state(PLUGIN_ID, "config.json") or {}
    if saved:
        _apply_config(saved)
    else:
        _add_row("id_mask")

    win.show()
    sin.output.append(
        "trigger-logger ready (multi OR triggers, time post, ASC/CSV)")


def deactivate():
    global _running
    _running = False
    _state["armed"] = False
    sin.output.append("trigger-logger deactivated")
