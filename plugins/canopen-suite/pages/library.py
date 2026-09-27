# -*- coding: utf-8 -*-
"""Profile library — shared CiA catalog → EDS draft (CANeds insert path)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QBrush, QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from _shared.canopen_profiles import (
    PROFILE_CATALOG,
    catalog_by_category,
    catalog_entry,
    missing_entries,
    objects_for,
    present_keys,
    search_profile,
)


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    tools = QHBoxLayout()
    tools.setContentsMargins(8, 6, 8, 4)
    search = QLineEdit()
    search.setPlaceholderText("Filter index or name")
    search.setFixedHeight(28)
    search.setMaximumWidth(180)
    search.setToolTip("Filter the active profile list")
    scope = QComboBox()
    scope.addItems(["All objects", "Missing only", "Already in draft"])
    scope.setFixedHeight(28)
    scope.setToolTip("Compare against the EDS draft")
    mode = QComboBox()
    mode.addItems(["Overwrite existing", "Skip existing"])
    mode.setFixedHeight(28)
    mode.setToolTip("When an index already exists in the draft")
    count = QLabel("0")
    count.setObjectName("SuiteCount")
    insert = QPushButton("Insert")
    insert.setObjectName("PrimaryButton")
    insert.setFixedHeight(28)
    insert.setCursor(Qt.CursorShape.PointingHandCursor)
    insert.setToolTip("Merge selected objects into the EDS draft")
    insert_miss = QPushButton("Insert missing")
    insert_miss.setObjectName("GhostButton")
    insert_miss.setFixedHeight(28)
    insert_miss.setCursor(Qt.CursorShape.PointingHandCursor)
    insert_all = QPushButton("Insert list")
    insert_all.setObjectName("GhostButton")
    insert_all.setFixedHeight(28)
    insert_all.setCursor(Qt.CursorShape.PointingHandCursor)
    goto = QPushButton("EDS")
    goto.setObjectName("GhostButton")
    goto.setFixedHeight(28)
    goto.setCursor(Qt.CursorShape.PointingHandCursor)
    goto.setToolTip("Open the EDS Dictionary")
    tools.addWidget(search)
    tools.addWidget(scope)
    tools.addWidget(mode)
    tools.addWidget(count)
    tools.addStretch(1)
    tools.addWidget(insert_miss)
    tools.addWidget(insert)
    tools.addWidget(insert_all)
    tools.addWidget(goto)
    layout.addLayout(tools)

    split = QSplitter(Qt.Orientation.Horizontal)
    split.setHandleWidth(1)
    cat_list = QListWidget()
    cat_list.setMinimumWidth(200)
    cat_list.setMaximumWidth(280)
    for category, rows in catalog_by_category().items():
        header = QListWidgetItem("— %s —" % category)
        header.setFlags(Qt.ItemFlag.NoItemFlags)
        header.setForeground(QBrush(QColor("#757575")))
        cat_list.addItem(header)
        for pid, title, kind, _cat, blurb in rows:
            short = title.replace("Pack — ", "") if kind == "pack" else title
            item = QListWidgetItem(short)
            item.setData(Qt.ItemDataRole.UserRole, pid)
            item.setToolTip(blurb)
            cat_list.addItem(item)
    split.addWidget(cat_list)

    right = QWidget()
    rl = QVBoxLayout(right)
    rl.setContentsMargins(8, 4, 8, 8)
    blurb = QLabel("")
    blurb.setWordWrap(True)
    blurb.setObjectName("SuiteQuiet")
    tree = QTreeWidget()
    tree.setHeaderLabels(["Status", "Index", "Name", "Access", "Default"])
    tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    tree.setAlternatingRowColors(True)
    tree.setRootIsDecorated(False)
    tree.header().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
    rl.addWidget(blurb)
    rl.addWidget(tree, 1)
    split.addWidget(right)
    split.setStretchFactor(0, 2)
    split.setStretchFactor(1, 5)
    layout.addWidget(split, 1)

    state = {"pid": "301"}

    # Feature keys for Side Bar nested nav (keep lib_301 / lib_402 aliases)
    feature_keys = ["library"]
    for pid, *_rest in PROFILE_CATALOG:
        feature_keys.append("lib_%s" % pid.replace("+", "_").replace(" ", "_"))
    parent._library_feature_keys = feature_keys
    parent._library_tabs = None

    def _overwrite() -> bool:
        return mode.currentIndex() == 0

    def _draft_keys():
        return present_keys(session.draft_entries)

    def refresh():
        pid = state["pid"] or "301"
        meta = catalog_entry(pid)
        blurb.setText(meta[4] if meta else "")
        tree.clear()
        rows = search_profile(search.text(), pid)
        have = _draft_keys()
        scope_i = scope.currentIndex()
        shown = miss = 0
        for e in rows:
            in_doc = (e.index, e.subindex) in have
            if scope_i == 1 and in_doc:
                continue
            if scope_i == 2 and not in_doc:
                continue
            status = "in draft" if in_doc else "missing"
            if not in_doc:
                miss += 1
            item = QTreeWidgetItem([
                status, e.display_index(), e.name,
                e.access_type or "", e.default_value or "",
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, e)
            item.setForeground(
                0, QBrush(QColor("#2E7D32" if in_doc else "#1565C0")))
            tree.addTopLevelItem(item)
            shown += 1
        count.setText("%d · %d miss" % (shown, miss))

    def _on_catalog():
        item = cat_list.currentItem()
        if not item:
            return
        pid = item.data(Qt.ItemDataRole.UserRole)
        if not pid:
            return
        state["pid"] = pid
        refresh()

    def _selected():
        out = []
        for item in tree.selectedItems():
            e = item.data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                out.append(e)
        return out

    def _visible():
        out = []
        for i in range(tree.topLevelItemCount()):
            e = tree.topLevelItem(i).data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                out.append(e)
        return out

    def _do_insert(entries, overwrite=None):
        if not entries:
            plugin_shell.set_status(parent, "Select one or more rows", 2500)
            return
        ow = _overwrite() if overwrite is None else overwrite
        stats = session.set_draft_from_library(
            entries, merge=True, overwrite=ow)
        msg = "Inserted +%d · ~%d · skip %d" % (
            stats.get("added", 0), stats.get("updated", 0),
            stats.get("skipped", 0))
        log_fn("RX", "-", b"", msg)
        plugin_shell.set_status(parent, msg, 3000)
        refresh()

    def select_view(key: str):
        # lib_301 / lib_402 / library / lib_<id>
        pid = "301"
        if key.startswith("lib_"):
            pid = key[4:].replace("_", " ")
            # packs with + may be encoded
            if pid == "RPDO1 TPDO1":
                pid = "RPDO1+TPDO1"
        elif key == "library":
            pid = "301"
        # Prefer exact catalog id match
        if catalog_entry(pid) is None and key.startswith("lib_"):
            raw = key[4:]
            if catalog_entry(raw):
                pid = raw
        state["pid"] = pid if catalog_entry(pid) else "301"
        for i in range(cat_list.count()):
            item = cat_list.item(i)
            if item.data(Qt.ItemDataRole.UserRole) == state["pid"]:
                cat_list.setCurrentRow(i)
                break
        refresh()

    def select_library_view(index: int):
        # legacy: 0=301, 1=402
        select_view("lib_301" if index == 0 else "lib_402")

    search.textChanged.connect(lambda _=None: refresh())
    scope.currentIndexChanged.connect(lambda _=None: refresh())
    cat_list.currentItemChanged.connect(lambda *_: _on_catalog())
    insert.clicked.connect(lambda: _do_insert(_selected()))
    insert_all.clicked.connect(lambda: _do_insert(_visible()))
    insert_miss.clicked.connect(
        lambda: _do_insert(
            missing_entries(state["pid"], session.draft_entries),
            overwrite=False))
    goto.clicked.connect(
        lambda: parent.goto_page("eds_dict")
        if hasattr(parent, "goto_page") else None)
    tree.itemDoubleClicked.connect(
        lambda item, _c: _do_insert([item.data(0, Qt.ItemDataRole.UserRole)]))

    for i in range(cat_list.count()):
        if cat_list.item(i).data(Qt.ItemDataRole.UserRole):
            cat_list.setCurrentRow(i)
            break

    root.select_library_view = select_library_view  # type: ignore[attr-defined]
    root.select_view = select_view  # type: ignore[attr-defined]
    return root
