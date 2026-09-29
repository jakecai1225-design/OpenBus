# -*- coding: utf-8 -*-
"""New EDS Wizard — Empty / Starters / From Profile (CANeds-style)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QSplitter,
    QStackedWidget,
    QTabWidget,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared.canopen_profiles import (
    PROFILE_CATALOG,
    STARTER_TEMPLATES,
    assemble_document,
    build_template,
)
from pages import _ui


def run_new_eds_wizard(parent, session) -> bool:
    """Show wizard; on accept load document into *session*. Returns True if created."""
    dlg = QDialog(parent)
    dlg.setWindowTitle("New EDS")
    dlg.setMinimumSize(720, 480)
    dlg.resize(800, 520)
    root = QVBoxLayout(dlg)
    root.setContentsMargins(12, 12, 12, 12)
    root.setSpacing(8)

    tip = QLabel(
        "Create an EDS document from a starter template or assemble CiA profiles "
        "(Vector CANeds workflow).")
    tip.setObjectName("SuiteHint")
    tip.setWordWrap(True)
    root.addWidget(tip)

    tabs = QTabWidget()
    root.addWidget(tabs, 1)

    # ---- Empty ----
    empty_page = QWidget()
    el = QVBoxLayout(empty_page)
    el.addWidget(QLabel(
        "Minimal CiA 301 shell with Identity, SDO server and heartbeat producer.\n"
        "Use when you will import device profiles yourself."))
    el.addStretch(1)
    tabs.addTab(empty_page, "Empty")

    # ---- Starters ----
    starters = QWidget()
    sl = QVBoxLayout(starters)
    sl.setContentsMargins(0, 0, 0, 0)
    s_split = QSplitter(Qt.Orientation.Horizontal)
    s_list = QListWidget()
    s_list.setMinimumWidth(180)
    for tid, title, blurb, _fn in STARTER_TEMPLATES:
        item = QListWidgetItem(title)
        item.setData(Qt.ItemDataRole.UserRole, tid)
        item.setToolTip(blurb)
        s_list.addItem(item)
    s_preview = QTreeWidget()
    s_preview.setHeaderLabels(["Index", "Name", "Access"])
    s_preview.setRootIsDecorated(False)
    _ui.configure_columns(s_preview, stretch=1)
    s_blurb = QLabel("")
    s_blurb.setObjectName("SuiteQuiet")
    s_blurb.setWordWrap(True)
    right_w = QWidget()
    right = QVBoxLayout(right_w)
    right.setContentsMargins(0, 0, 0, 0)
    right.addWidget(s_blurb)
    right.addWidget(s_preview, 1)
    s_split.addWidget(s_list)
    s_split.addWidget(right_w)
    _ui.configure_splitter(s_split, golden=True, master_left=False)
    sl.addWidget(s_split, 1)
    tabs.addTab(starters, "Starters")

    def _preview_starter():
        s_preview.clear()
        item = s_list.currentItem()
        if not item:
            s_blurb.setText("Select a starter")
            return
        tid = item.data(Qt.ItemDataRole.UserRole)
        meta = next((t for t in STARTER_TEMPLATES if t[0] == tid), None)
        s_blurb.setText(meta[2] if meta else "")
        doc = build_template(tid)
        for e in doc.entries[:200]:
            s_preview.addTopLevelItem(QTreeWidgetItem([
                e.display_index(), e.name or "", e.access_type or ""]))
        if len(doc.entries) > 200:
            s_preview.addTopLevelItem(QTreeWidgetItem([
                "…", "%d more" % (len(doc.entries) - 200), ""]))
        _ui.fit_columns(s_preview, stretch=1)

    s_list.currentItemChanged.connect(lambda *_: _preview_starter())
    if s_list.count():
        s_list.setCurrentRow(0)

    # ---- From Profile ----
    profile_page = QWidget()
    pl = QVBoxLayout(profile_page)
    row = QHBoxLayout()
    base_box = QComboBox()
    base_box.setFixedHeight(_ui.CTRL_H)
    device_box = QComboBox()
    device_box.setFixedHeight(_ui.CTRL_H)
    base_box.addItem("CiA 301 (communication)", "301")
    base_box.addItem("CiA 302 (NMT manager)", "302")
    device_box.addItem("(none — communication only)", "")
    for pid, title, kind, cat, _blurb in PROFILE_CATALOG:
        if kind == "profile" and cat == "Device":
            device_box.addItem(title, pid)
    row.addWidget(QLabel("Base"))
    row.addWidget(base_box, 1)
    row.addWidget(QLabel("Device"))
    row.addWidget(device_box, 1)
    pl.addLayout(row)

    packs_host = QWidget()
    packs_lay = QVBoxLayout(packs_host)
    packs_lay.setContentsMargins(0, 4, 0, 4)
    packs_lay.addWidget(QLabel("Packs (optional)"))
    pack_checks: list[tuple[str, QCheckBox]] = []
    for pid, title, kind, _cat, blurb in PROFILE_CATALOG:
        if kind != "pack":
            continue
        chk = QCheckBox(title.replace("Pack — ", ""))
        chk.setToolTip(blurb)
        # Sensible defaults for first wiring
        if pid in ("RPDO1+TPDO1", "Heartbeat producer", "SDO server", "Identity"):
            chk.setChecked(True)
        packs_lay.addWidget(chk)
        pack_checks.append((pid, chk))
    packs_lay.addStretch(1)
    p_split = QSplitter(Qt.Orientation.Horizontal)
    packs_host.setMinimumWidth(160)
    p_preview = QTreeWidget()
    p_preview.setHeaderLabels(["Index", "Name"])
    p_preview.setRootIsDecorated(False)
    _ui.configure_columns(p_preview, stretch=1)
    p_count = QLabel("0 objects")
    p_count.setObjectName("SuiteCount")
    pr_w = QWidget()
    pr = QVBoxLayout(pr_w)
    pr.setContentsMargins(0, 0, 0, 0)
    pr.addWidget(p_count)
    pr.addWidget(p_preview, 1)
    p_split.addWidget(packs_host)
    p_split.addWidget(pr_w)
    _ui.configure_splitter(p_split, golden=True, master_left=False)
    pl.addWidget(p_split, 1)
    tabs.addTab(profile_page, "From Profile")

    def _preview_profile():
        p_preview.clear()
        packs = [pid for pid, chk in pack_checks if chk.isChecked()]
        device = device_box.currentData() or None
        doc = assemble_document(
            base=base_box.currentData() or "301",
            device=device,
            packs=packs,
            product="New Device")
        p_count.setText("%d objects" % len(doc.entries))
        for e in doc.entries[:200]:
            p_preview.addTopLevelItem(QTreeWidgetItem([
                e.display_index(), e.name or ""]))
        if len(doc.entries) > 200:
            p_preview.addTopLevelItem(QTreeWidgetItem([
                "…", "%d more" % (len(doc.entries) - 200)]))
        _ui.fit_columns(p_preview, stretch=1)

    base_box.currentIndexChanged.connect(lambda *_: _preview_profile())
    device_box.currentIndexChanged.connect(lambda *_: _preview_profile())
    for _pid, chk in pack_checks:
        chk.toggled.connect(lambda *_: _preview_profile())
    _preview_profile()

    buttons = QDialogButtonBox(
        QDialogButtonBox.StandardButton.Ok
        | QDialogButtonBox.StandardButton.Cancel)
    buttons.button(QDialogButtonBox.StandardButton.Ok).setText("Create")
    root.addWidget(buttons)

    def _accept():
        page = tabs.currentIndex()
        if page == 0:
            session.new_empty()
        elif page == 1:
            item = s_list.currentItem()
            tid = item.data(Qt.ItemDataRole.UserRole) if item else "generic"
            session.new_from_template(tid or "generic")
        else:
            packs = [pid for pid, chk in pack_checks if chk.isChecked()]
            device = device_box.currentData() or None
            session.new_from_profile(
                base=base_box.currentData() or "301",
                device=device,
                packs=packs,
                product="New Device")
        dlg.accept()

    def _on_starter_dbl(_item):
        tabs.setCurrentIndex(1)
        _accept()

    s_list.itemDoubleClicked.connect(_on_starter_dbl)
    buttons.accepted.connect(_accept)
    buttons.rejected.connect(dlg.reject)

    # Prefer Starters tab (CANeds beginner path)
    tabs.setCurrentIndex(1)
    return dlg.exec() == QDialog.DialogCode.Accepted
