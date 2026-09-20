# -*- coding: utf-8 -*-
"""Network workspace — Topology and AL state as editor tabs."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QButtonGroup,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QStackedWidget,
    QTabBar,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from core.al_state import request_state
from session import Slave
from widgets import ghost, primary, spin, table


def _topology(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    row = QHBoxLayout()
    add = ghost("Add slave", "add", "Append a slave in INIT")
    remove = ghost("Remove", "delete", "Remove the selected slave")
    demo = ghost("Load demo", "refresh", "Replace the list with the bundled ESI slave")
    row.addWidget(add)
    row.addWidget(remove)
    row.addWidget(demo)
    row.addStretch(1)
    root.addLayout(row)
    grid = table(["Pos", "Name", "Vendor", "Product", "AL", "Cable m"])
    root.addWidget(grid, 1)

    def refill():
        grid.setRowCount(0)
        for slave in session.slaves:
            r = grid.rowCount()
            grid.insertRow(r)
            vals = (
                str(slave.position),
                slave.name,
                "0x%X" % slave.vendor_id,
                "0x%X" % slave.product_code,
                slave.state,
                "%.2f" % slave.cable_m,
            )
            for c, text in enumerate(vals):
                grid.setItem(r, c, QTableWidgetItem(text))
        if session.slaves:
            grid.selectRow(session.selected)

    def on_select():
        row_i = grid.currentRow()
        if row_i >= 0:
            session.selected = row_i

    def do_add():
        pos = len(session.slaves)
        session.slaves.append(Slave("Slave %d" % pos, position=pos))
        session.selected = pos
        session.notify()

    def do_remove():
        if not session.slaves:
            return
        session.selected = max(0, min(session.selected, len(session.slaves) - 1))
        del session.slaves[session.selected]
        session.selected = max(0, session.selected - 1)
        session.notify()

    add.clicked.connect(do_add)
    remove.clicked.connect(do_remove)
    demo.clicked.connect(session.load_sample)
    grid.itemSelectionChanged.connect(on_select)
    session.on_changed(refill)
    refill()
    return page


def _states(session):
    page = QWidget()
    root = QVBoxLayout(page)
    root.setContentsMargins(12, 8, 12, 8)
    root.setSpacing(8)
    head = QHBoxLayout()
    label = QLabel("INIT")
    label.setObjectName("SessionBadge")
    head.addWidget(label)
    group = QButtonGroup(page)
    group.setExclusive(True)
    buttons = {}
    for name in ("INIT", "PREOP", "SAFEOP", "OP", "BOOT"):
        btn = QPushButton(name)
        btn.setObjectName("SegmentBtn")
        btn.setCheckable(True)
        btn.setFixedHeight(28)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setToolTip("Request %s if the transition is allowed" % name)
        group.addButton(btn)
        buttons[name] = btn
        head.addWidget(btn)
    head.addStretch(1)
    root.addLayout(head)
    cable = spin(0, 1000, 1, "Cable length from the previous slave, metres")
    apply_cable = primary("Set cable", "Store cable length on the selected slave")
    row = QHBoxLayout()
    row.addWidget(cable)
    row.addWidget(apply_cable)
    row.addStretch(1)
    root.addLayout(row)
    root.addStretch(1)

    def show():
        slave = session.current()
        if slave is None:
            label.setText("No slave")
            return
        label.setText("%s  %s" % (slave.name, slave.state))
        btn = buttons.get(slave.state)
        if btn is not None:
            btn.setChecked(True)
        cable.setValue(int(slave.cable_m))

    def request(name):
        slave = session.current()
        if slave is None:
            return
        result = request_state(slave.state, name)
        if not result["ok"]:
            session.log("ERR", slave.position, b"", "AL %s -> %s refused" % (slave.state, name))
            show()
            return
        slave.state = result["state"]
        session.log("SYS", slave.position, b"", "AL %s" % slave.state)
        session.notify()

    def set_cable():
        slave = session.current()
        if slave is None:
            return
        slave.cable_m = float(cable.value())
        session.notify()

    for name, btn in buttons.items():
        btn.clicked.connect(lambda _c=False, n=name: request(n))
    apply_cable.clicked.connect(set_cable)
    session.on_changed(show)
    show()
    return page


def build(parent, session, _log):
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.addTab("Topology")
    bar.addTab("State")
    stack = QStackedWidget()
    stack.addWidget(_topology(session))
    stack.addWidget(_states(session))
    bar.currentChanged.connect(stack.setCurrentIndex)
    parent._net_tabs = bar
    return stack
