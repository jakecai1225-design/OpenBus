# -*- coding: utf-8 -*-
"""can-bit-timing — Classic CAN / CAN FD bit-timing calculator.

Clock + target baud + sample point → BRP/TSEG1/TSEG2/SJW candidates,
FD dual-phase + TDC estimate, frame-time / bus utilization, presets, copy.
Offline tool — no bus I/O.
"""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QTreeWidget, QTreeWidgetItem, QHeaderView, QGroupBox,
    QFormLayout, QDoubleSpinBox, QComboBox, QSpinBox, QTabWidget,
    QApplication, QMessageBox,
)

import sin
from _shared import plugin_shell, state_store

SUITE_ID = "bus-utilities"
PAGE_KEY = "bit_timing"

PRESETS = [
    ("Classic 500 kbps @ 40 MHz", {"mode": 0, "clock": 40.0, "arb": "500",
                                   "data": "2000", "sp_arb": 87.5, "sp_data": 70.0}),
    ("Classic 250 kbps @ 16 MHz", {"mode": 0, "clock": 16.0, "arb": "250",
                                   "data": "2000", "sp_arb": 87.5, "sp_data": 70.0}),
    ("Classic 125 kbps @ 8 MHz", {"mode": 0, "clock": 8.0, "arb": "125",
                                  "data": "2000", "sp_arb": 87.5, "sp_data": 70.0}),
    ("FD 500/2000 @ 40 MHz", {"mode": 1, "clock": 40.0, "arb": "500",
                              "data": "2000", "sp_arb": 80.0, "sp_data": 70.0}),
    ("FD 500/4000 @ 80 MHz", {"mode": 1, "clock": 80.0, "arb": "500",
                              "data": "4000", "sp_arb": 80.0, "sp_data": 75.0}),
    ("FD 1000/5000 @ 80 MHz", {"mode": 1, "clock": 80.0, "arb": "1000",
                               "data": "5000", "sp_arb": 75.0, "sp_data": 70.0}),
]

_win = None


def solve_bit_timing(clock_mhz, baud_kbps, sample_pct, n_min=8, n_max=40):
    """Search (BRP, N, TSEG1, TSEG2, SJW) solutions sorted by error."""
    results = []
    target = baud_kbps * 1000.0
    for n in range(n_min, n_max + 1):
        brp_f = clock_mhz * 1e6 / (n * target)
        brp = round(brp_f)
        if brp < 1 or brp > 1024:
            continue
        actual = clock_mhz * 1e6 / (n * brp)
        err = abs(actual - target) / target * 100.0
        if err > 2.0:
            continue
        tseg1 = round(sample_pct / 100.0 * n) - 1
        tseg2 = n - 1 - tseg1
        if tseg1 < 1 or tseg2 < 1:
            continue
        sjw = min(tseg2, 4)
        sp = 100.0 * (1 + tseg1) / n
        results.append({
            "brp": brp, "n": n, "tseg1": tseg1, "tseg2": tseg2, "sjw": sjw,
            "baud": actual / 1000.0, "err": err, "sp": sp,
            "sp_err": abs(sp - sample_pct)})
    results.sort(key=lambda r: (round(r["err"], 3), r["sp_err"], r["n"]))
    seen_n = set()
    dedup = []
    for r in results:
        if r["n"] in seen_n:
            continue
        seen_n.add(r["n"])
        dedup.append(r)
    return dedup[:6]


def frame_time_ms(baud_kbps, dlc, extended, fd, data_baud_kbps=None):
    """Frame duration estimate including stuff-bit approximation."""
    if fd:
        arb = baud_kbps
        dat = data_baud_kbps or baud_kbps
        bits_arb = 1 + (29 if extended else 11) + 2
        bits_data = 20 + 8 * dlc + (21 if dlc > 16 else (17 if dlc > 8 else 15))
        stuff = (bits_arb + bits_data) / 5.0
        return (bits_arb / (arb * 1000.0) + (bits_data + stuff) / (dat * 1000.0)) * 1000.0
    bits = 1 + (29 if extended else 11) + 1 + 1 + 1 + 4 + 8 * dlc + 15 + 1 + 2 + 7 + 3
    stuff = (bits - 10) / 5.0
    return (bits + stuff) / (baud_kbps * 1000.0) * 1000.0


def _fmt_row(r):
    return (
        "BRP=%d N=%d TSEG1=%d TSEG2=%d SJW=%d "
        "baud=%.3f kbps err=%.3f%% SP=%.1f%%"
        % (r["brp"], r["n"], r["tseg1"], r["tseg2"], r["sjw"],
           r["baud"], r["err"], r["sp"])
    )


def build(parent, session, log_fn):
    root = QWidget(parent)
    layout = QVBoxLayout(root)

    cfg = QGroupBox("Parameters")
    cfg_l = QFormLayout(cfg)
    preset_combo = QComboBox()
    preset_combo.addItem("Custom…")
    for name, _ in PRESETS:
        preset_combo.addItem(name)
    cfg_l.addRow("Preset:", preset_combo)

    clock_spin = QDoubleSpinBox()
    clock_spin.setRange(1, 200)
    clock_spin.setDecimals(1)
    clock_spin.setValue(40)
    clock_spin.setSuffix(" MHz")
    cfg_l.addRow("Controller clock:", clock_spin)

    mode_combo = QComboBox()
    mode_combo.addItems(["Classic CAN", "CAN FD (arb + data phases)"])
    cfg_l.addRow("Mode:", mode_combo)

    arb_baud = QComboBox()
    arb_baud.setEditable(True)
    arb_baud.addItems(["50", "100", "125", "250", "500", "800", "1000", "2000", "5000"])
    arb_baud.setCurrentText("500")
    cfg_l.addRow("Arbitration baud (kbps):", arb_baud)

    data_baud = QComboBox()
    data_baud.setEditable(True)
    data_baud.addItems(["1000", "2000", "4000", "5000", "8000"])
    data_baud.setCurrentText("2000")
    cfg_l.addRow("Data baud (kbps):", data_baud)

    sp_arb = QDoubleSpinBox()
    sp_arb.setRange(50, 95)
    sp_arb.setValue(87.5)
    sp_arb.setSuffix(" %")
    cfg_l.addRow("Arbitration sample point:", sp_arb)

    sp_data = QDoubleSpinBox()
    sp_data.setRange(50, 95)
    sp_data.setValue(70.0)
    sp_data.setSuffix(" %")
    cfg_l.addRow("Data sample point:", sp_data)
    layout.addWidget(cfg)

    btn_row = QHBoxLayout()
    calc_btn = QPushButton("Calculate")
    copy_btn = QPushButton("Copy selected")
    copy_best_btn = QPushButton("Copy best result")
    btn_row.addWidget(calc_btn)
    btn_row.addWidget(copy_btn)
    btn_row.addWidget(copy_best_btn)
    btn_row.addStretch(1)
    layout.addLayout(btn_row)

    layout.addWidget(plugin_shell.help_label(
        "Offline calculator — no bus I/O. Typical sample points: Classic 87.5%, "
        "FD arb 75–80%, FD data 60–80%. Select a row and Copy, or Copy best."))

    tabs = QTabWidget()
    layout.addWidget(tabs, 1)

    arb_tree = QTreeWidget()
    arb_tree.setHeaderLabels([
        "BRP", "N(tq)", "TSEG1", "TSEG2", "SJW",
        "Actual kbps", "Error %", "Sample %"])
    arb_tree.setRootIsDecorated(False)
    arb_tree.setAlternatingRowColors(True)
    arb_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tabs.addTab(arb_tree, "Arbitration")

    data_tree = QTreeWidget()
    data_tree.setHeaderLabels([
        "BRP", "N(tq)", "TSEG1", "TSEG2", "SJW",
        "Actual kbps", "Error %", "Sample %"])
    data_tree.setRootIsDecorated(False)
    data_tree.setAlternatingRowColors(True)
    data_tree.header().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
    tabs.addTab(data_tree, "Data (FD)")

    tdc_box = QGroupBox("TDC (CAN FD data phase)")
    tdc_l = QFormLayout(tdc_box)
    loop_spin = QSpinBox()
    loop_spin.setRange(0, 2000)
    loop_spin.setValue(255)
    loop_spin.setSuffix(" ns loop delay")
    tdc_l.addRow("Transceiver loop delay:", loop_spin)
    tdc_result = QLabel("Click Calculate")
    tdc_result.setWordWrap(True)
    tdc_l.addRow("Estimate:", tdc_result)
    tabs.addTab(tdc_box, "TDC")

    frame_box = QGroupBox("Frame time & utilization")
    frame_l = QFormLayout(frame_box)
    dlc_combo = QComboBox()
    dlc_combo.addItems([
        "0", "1", "2", "3", "4", "5", "6", "7", "8",
        "12", "16", "20", "24", "32", "48", "64"])
    dlc_combo.setCurrentIndex(8)
    frame_l.addRow("DLC:", dlc_combo)
    frame_result = QLabel("Click Calculate")
    frame_result.setWordWrap(True)
    frame_l.addRow("Estimate:", frame_result)
    tabs.addTab(frame_box, "Frame time")

    store = {"arb_rows": [], "data_rows": []}

    def _fill(tree, rows):
        tree.clear()
        for r in rows:
            tree.addTopLevelItem(QTreeWidgetItem([
                str(r["brp"]), str(r["n"]), str(r["tseg1"]), str(r["tseg2"]),
                str(r["sjw"]), "%.3f" % r["baud"], "%.3f" % r["err"],
                "%.1f" % r["sp"]]))

    def _persist():
        state_store.save_state(SUITE_ID, {
            "clock": clock_spin.value(),
            "mode": mode_combo.currentIndex(),
            "arb": arb_baud.currentText(),
            "data": data_baud.currentText(),
            "sp_arb": sp_arb.value(),
            "sp_data": sp_data.value(),
            "loop_ns": loop_spin.value(),
            "dlc": dlc_combo.currentText(),
            "preset": preset_combo.currentIndex(),
        })

    def _apply_preset(idx):
        if idx <= 0:
            return
        p = PRESETS[idx - 1][1]
        mode_combo.setCurrentIndex(p["mode"])
        clock_spin.setValue(p["clock"])
        arb_baud.setCurrentText(p["arb"])
        data_baud.setCurrentText(p["data"])
        sp_arb.setValue(p["sp_arb"])
        sp_data.setValue(p["sp_data"])

    def _on_calc():
        try:
            clock = clock_spin.value()
            arb_kbps = float(arb_baud.currentText())
            data_kbps = float(data_baud.currentText())
        except ValueError:
            plugin_shell.set_status(parent, "Invalid baud rate", 3000)
            return
        arb_rows = solve_bit_timing(clock, arb_kbps, sp_arb.value(), 8, 25)
        store["arb_rows"] = arb_rows
        _fill(arb_tree, arb_rows)
        if not arb_rows:
            plugin_shell.set_status(parent, "No arbitration solution within 2% error", 4000)
        if mode_combo.currentIndex() == 1:
            data_rows = solve_bit_timing(clock, data_kbps, sp_data.value(), 8, 40)
            store["data_rows"] = data_rows
            _fill(data_tree, data_rows)
            if not data_rows:
                plugin_shell.set_status(parent, "No data-phase solution within 2% error", 4000)
        else:
            store["data_rows"] = []
            data_tree.clear()

        if data_tree.topLevelItemCount():
            item = data_tree.topLevelItem(0)
            n = int(item.text(1))
            brp = int(item.text(0))
            tq_ns = brp / clock * 1000.0
            bit_ns = tq_ns * n
            tdco_ns = loop_spin.value() + bit_ns
            tdco_tq = tdco_ns / tq_ns
            tdc_result.setText(
                "Data 1 tq ≈ %.1f ns · data bit ≈ %.0f ns\n"
                "Suggested TDCO ≈ loop + 1 data bit ≈ %d ns ≈ %.1f tq"
                % (tq_ns, bit_ns, tdco_ns, tdco_tq))
        else:
            tdc_result.setText("TDC only applies in CAN FD mode")

        try:
            dlc = int(dlc_combo.currentText())
        except ValueError:
            dlc = 8
        classic_ms = frame_time_ms(arb_kbps, min(dlc, 8), False, False)
        ext_ms = frame_time_ms(arb_kbps, min(dlc, 8), True, False)
        parts = [
            "Classic standard (8B) ≈ %.3f ms (≈%.0f fps @100%% load)"
            % (classic_ms, 1000.0 / classic_ms),
            "Classic extended (8B) ≈ %.3f ms (≈%.0f fps @100%% load)"
            % (ext_ms, 1000.0 / ext_ms),
        ]
        if mode_combo.currentIndex() == 1:
            fd_ms = frame_time_ms(arb_kbps, dlc, False, True, data_kbps)
            parts.append(
                "FD frame (DLC %d, data %.0f kbps) ≈ %.3f ms (≈%.0f fps)"
                % (dlc, data_kbps, fd_ms, 1000.0 / fd_ms))
        frame_result.setText("\n".join(parts))
        _persist()
        n = len(arb_rows) + len(store["data_rows"])
        plugin_shell.set_status(parent, "Calculated %d candidate(s)" % n, 3000)

    def _copy_text(text):
        QApplication.clipboard().setText(text)
        plugin_shell.set_status(parent, "Copied to clipboard", 2500)

    def _on_copy_selected():
        tree = arb_tree if tabs.currentIndex() == 0 else data_tree
        rows = store["arb_rows"] if tabs.currentIndex() == 0 else store["data_rows"]
        items = tree.selectedItems()
        if not items or not rows:
            QMessageBox.information(parent, "Copy", "Select a timing row first")
            return
        idx = tree.indexOfTopLevelItem(items[0])
        if 0 <= idx < len(rows):
            phase = "arb" if tabs.currentIndex() == 0 else "data"
            _copy_text("[%s] %s" % (phase, _fmt_row(rows[idx])))

    def _on_copy_best():
        parts = []
        if store["arb_rows"]:
            parts.append("[arb] %s" % _fmt_row(store["arb_rows"][0]))
        if store["data_rows"]:
            parts.append("[data] %s" % _fmt_row(store["data_rows"][0]))
        if not parts:
            QMessageBox.information(parent, "Copy", "Calculate first")
            return
        _copy_text("\n".join(parts))

    calc_btn.clicked.connect(_on_calc)
    copy_btn.clicked.connect(_on_copy_selected)
    copy_best_btn.clicked.connect(_on_copy_best)
    preset_combo.currentIndexChanged.connect(lambda i: (_apply_preset(i), _on_calc()))
    plugin_shell.bind_shortcut(parent, "Ctrl+Return", _on_calc)
    plugin_shell.bind_shortcut(parent, "Ctrl+C", _on_copy_selected)


    saved = state_store.load_state(SUITE_ID, default={}) or {}
    if saved:
        clock_spin.setValue(float(saved.get("clock", 40)))
        mode_combo.setCurrentIndex(int(saved.get("mode", 0)))
        arb_baud.setCurrentText(str(saved.get("arb", "500")))
        data_baud.setCurrentText(str(saved.get("data", "2000")))
        sp_arb.setValue(float(saved.get("sp_arb", 87.5)))
        sp_data.setValue(float(saved.get("sp_data", 70.0)))
        loop_spin.setValue(int(saved.get("loop_ns", 255)))
        dlc = str(saved.get("dlc", "8"))
        if dlc_combo.findText(dlc) >= 0:
            dlc_combo.setCurrentText(dlc)

    _on_calc()

    return root
