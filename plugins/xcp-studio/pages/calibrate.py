# -*- coding: utf-8 -*-
"""Calibrate — scalar / MAP grid + calibration page switch."""

from __future__ import annotations

from PyQt6.QtWidgets import (
    QFormLayout,
    QMessageBox,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from pages import _ui


def build(shell, session, log_fn) -> QWidget:
    root = QWidget()
    lay = QVBoxLayout(root)
    lay.setContentsMargins(12, 8, 12, 8)
    lay.setSpacing(8)

    form_host = QWidget()
    form_host.setMaximumWidth(480)
    form = QFormLayout(form_host)
    form.setContentsMargins(0, 0, 0, 0)

    name_box = _ui.combo([], "CHARACTERISTIC to write")
    value_edit = _ui.line_edit("Scalar value", "0")
    page_box = _ui.combo(["0", "1", "2", "3"], "SET_CAL_PAGE")
    write_btn = _ui.primary_btn("Write scalar", "DOWNLOAD after confirm", "edit")
    page_btn = _ui.ghost_btn("Switch page", "SET_CAL_PAGE", "layers")
    map_write = _ui.ghost_btn("Write cell", "Selected MAP cell", "table")
    form.addRow("Characteristic", name_box)
    form.addRow("Value", value_edit)
    form.addRow("Cal page", page_box)

    grid = QTableWidget(4, 8)
    grid.setMaximumHeight(180)
    for c in range(8):
        grid.setHorizontalHeaderItem(c, QTableWidgetItem(str(c)))
    for r in range(4):
        grid.setVerticalHeaderItem(r, QTableWidgetItem(str(r)))
        for c in range(8):
            grid.setItem(r, c, QTableWidgetItem("0"))

    lay.addWidget(form_host)
    lay.addWidget(_ui.tool_strip(write_btn, page_btn, map_write, stretch_at=3))
    lay.addWidget(_ui.muted_label("MAP grid (4×8 lite — sequential addresses)"))
    lay.addWidget(grid)
    lay.addStretch(1)

    def refresh_names():
        cur = name_box.currentText()
        name_box.blockSignals(True)
        name_box.clear()
        names = [s.name for s in session.symbols() if s.kind == "CHARACTERISTIC"]
        name_box.addItems(names or ["(none)"])
        if cur and cur in names:
            name_box.setCurrentText(cur)
        name_box.blockSignals(False)

    def on_write():
        name = name_box.currentText()
        if not name or name == "(none)":
            return
        try:
            val = float(value_edit.text() or "0")
        except ValueError:
            log_fn("ERR", "-", b"", "Invalid value")
            return
        if QMessageBox.question(
                shell, "Write characteristic",
                "Write %s = %s ?" % (name, value_edit.text()),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No) != QMessageBox.StandardButton.Yes:
            return

        def done(ok, _p, note):
            log_fn("SYS" if ok else "ERR", "-", b"", note or "DOWNLOAD")

        session.write_characteristic(name, val, on_done=done)

    def on_page():
        page = int(page_box.currentText() or "0")

        def done(ok, _p, note):
            log_fn("SYS" if ok else "ERR", "-", b"", note or "SET_CAL_PAGE")

        session.switch_cal_page(page, on_done=done)

    def on_map():
        name = name_box.currentText()
        if not name or name == "(none)":
            return
        item = grid.currentItem()
        if item is None:
            log_fn("ERR", "-", b"", "Select a MAP cell")
            return
        try:
            val = float(item.text() or "0")
        except ValueError:
            log_fn("ERR", "-", b"", "Invalid cell")
            return
        col, row = grid.currentColumn(), grid.currentRow()
        if QMessageBox.question(
                shell, "Write MAP cell",
                "Write %s[%d,%d] = %s ?" % (name, row, col, item.text()),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No) != QMessageBox.StandardButton.Yes:
            return

        def done(ok, _p, note):
            log_fn("SYS" if ok else "ERR", "-", b"", note or "MAP DOWNLOAD")

        session.write_map_cell(name, col, row, val, on_done=done)

    write_btn.clicked.connect(on_write)
    page_btn.clicked.connect(on_page)
    map_write.clicked.connect(on_map)
    session.on_a2l_changed(refresh_names)
    refresh_names()
    return root
