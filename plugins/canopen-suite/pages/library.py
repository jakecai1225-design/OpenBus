# -*- coding: utf-8 -*-
"""Profile library — shared CiA catalog → EDS draft (CANeds insert path)."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QBrush, QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QLabel,
    QLineEdit,
    QListWidgetItem,
    QSplitter,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import plugin_shell
from pages import _ui, interop
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

    search = QLineEdit()
    search.setPlaceholderText("Filter index or name…")
    search.setClearButtonEnabled(True)
    search.setFixedHeight(_ui.CTRL_H)
    search.setToolTip("Filter the active profile list")
    scope = QComboBox()
    scope.addItems(["All", "Missing", "In draft"])
    scope.setFixedHeight(_ui.CTRL_H)
    scope.setMaximumWidth(100)
    scope.setToolTip("Compare against the EDS draft")
    mode = QComboBox()
    mode.addItems(["Overwrite", "Skip existing"])
    mode.setFixedHeight(_ui.CTRL_H)
    mode.setMaximumWidth(110)
    mode.setToolTip("When an index already exists in the draft")
    count = QLabel("0")
    count.setObjectName("SuiteCount")
    count.setFixedHeight(_ui.CTRL_H)
    count.setAlignment(
        Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft)
    insert = _ui.primary_btn(
        "Insert", "Merge selected objects into the EDS draft", "add")
    insert.setMaximumWidth(96)
    insert_miss = _ui.ghost_btn(
        "Missing", "Insert all objects not yet in the draft", "checklist")

    split = QSplitter(Qt.Orientation.Horizontal)
    cat_list = interop.ProfileCatalogList()
    cat_list.setMinimumWidth(_ui.PANE_MIN_PROP)
    cat_list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    _ui.style_list(cat_list)
    for category, rows in catalog_by_category().items():
        header = QListWidgetItem("— %s —" % category)
        header.setFlags(Qt.ItemFlag.NoItemFlags)
        header.setForeground(QBrush(QColor("#757575")))
        cat_list.addItem(header)
        for pid, title, kind, _cat, blurb in rows:
            short = title.replace("Pack — ", "") if kind == "pack" else title
            item = QListWidgetItem(short)
            item.setData(Qt.ItemDataRole.UserRole, pid)
            item.setToolTip(blurb + "\nDrag onto EDS Dictionary to insert")
            cat_list.addItem(item)
    split.addWidget(cat_list)

    right = QWidget()
    rl = QVBoxLayout(right)
    rl.setContentsMargins(0, 0, 0, 0)
    rl.setSpacing(0)
    rl.addWidget(_ui.inline_filter(
        search, scope, mode, count, insert_miss, insert))
    tree = QTreeWidget()
    tree.setHeaderLabels(["Status", "Index", "Name", "Access", "Default"])
    _ui.style_tree(tree, header_hidden=False)
    tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    tree.setAlternatingRowColors(True)
    tree.setRootIsDecorated(False)
    _ui.configure_columns(
        tree, stretch=2,
        mins={0: 72, 1: 72, 2: 120, 3: 56, 4: 64})
    rl.addWidget(tree, 1)
    next_open = _ui.ghost_btn(
        "Open Objects", "Open the EDS Dictionary", "eds")
    next_map = _ui.primary_btn(
        "Map in PDO", "Open PDO map after insert", "flow")
    next_bar = _ui.next_step_bar(
        "Inserted — continue:", next_open, next_map)
    next_bar.setVisible(False)
    rl.addWidget(next_bar)
    split.addWidget(right)
    # Catalog (secondary) | profile matrix (primary) → golden minor : major
    _ui.configure_splitter(split, golden=True, master_left=False)
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
        tree.setToolTip(meta[4] if meta else "")
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
        _ui.fit_columns(tree, stretch=2)

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

    def _ensure_draft() -> bool:
        if session.draft_entries:
            return True
        from PyQt6.QtWidgets import QMessageBox
        reply = QMessageBox.question(
            parent, "No EDS draft",
            "No EDS document is open.\nCreate one with the New EDS wizard?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.Yes)
        if reply == QMessageBox.StandardButton.Yes and hasattr(parent, "new_eds"):
            parent.new_eds()
        return bool(session.draft_entries)

    def _expand_for_insert(entries):
        """If a RECORD/ARRAY shell is selected, also insert its sub-entries."""
        from _shared.canopen_profiles import objects_for
        pool = objects_for(state["pid"] or "301")
        by_idx = {}
        for e in pool:
            by_idx.setdefault(e.index, []).append(e)
        out = []
        seen = set()
        for e in entries:
            ot = str(getattr(e, "object_type", "") or "").lower()
            shell = ot in ("0x8", "0x9", "array", "record") and e.subindex == 0
            group = by_idx.get(e.index) if shell else None
            for sib in (group or (e,)):
                key = (sib.index, sib.subindex)
                if key in seen:
                    continue
                seen.add(key)
                out.append(sib)
        return out

    def _do_insert(entries, overwrite=None):
        if not entries:
            plugin_shell.set_status(parent, "Select one or more rows", 2500)
            return
        if not _ensure_draft():
            return
        ow = _overwrite() if overwrite is None else overwrite
        expanded = _expand_for_insert(entries)
        stats = session.set_draft_from_library(
            expanded, merge=True, overwrite=ow)
        msg = "Inserted +%d · ~%d · skip %d" % (
            stats.get("added", 0), stats.get("updated", 0),
            stats.get("skipped", 0))
        log_fn("RX", "-", b"", msg)
        plugin_shell.set_status(parent, msg, 3000)
        refresh()
        # Focus first inserted object for carry into Objects / PDO.
        if expanded and hasattr(session, "set_focus"):
            e0 = expanded[0]
            session.set_focus(e0.index, e0.subindex)
        next_bar.setVisible(True)
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds_dict")
            eds = parent._pages.get("eds")
            if eds is not None and hasattr(eds, "focus_object") and expanded:
                e0 = expanded[0]
                eds.focus_object(e0.index, e0.subindex)

    def _use_as_new():
        from _shared.canopen_profiles import catalog_entry
        pid = state["pid"] or "301"
        meta = catalog_entry(pid)
        kind = meta[2] if meta else "profile"
        if kind == "pack":
            session.new_from_profile(base="301", packs=(pid,), product="New Device")
        elif pid in ("301", "302"):
            session.new_from_profile(base=pid, product="New Device")
        else:
            session.new_from_profile(
                base="301", device=pid, packs=("Identity", "SDO server"),
                product=meta[1] if meta else "New Device")
        log_fn("SYS", "-", b"", "New document from profile %s" % pid)
        if hasattr(parent, "goto_page"):
            parent.goto_page("eds_dict")

    def select_view(key: str):
        # lib_301 / lib_402 / library / profiles / lib_<id>
        pid = "301"
        if key in ("library", "profiles"):
            pid = "301"
        elif key.startswith("lib_"):
            pid = key[4:].replace("_", " ")
            # packs with + may be encoded
            if pid == "RPDO1 TPDO1":
                pid = "RPDO1+TPDO1"
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
    insert_miss.clicked.connect(
        lambda: _do_insert(
            missing_entries(state["pid"], session.draft_entries),
            overwrite=False))
    tree.itemDoubleClicked.connect(
        lambda item, _c: _do_insert([item.data(0, Qt.ItemDataRole.UserRole)]))
    next_open.clicked.connect(
        lambda: interop.shell_action(parent, "view.eds"))
    next_map.clicked.connect(
        lambda: interop.shell_action(parent, "view.pdo_map"))

    def _cat_menu(pos):
        from PyQt6.QtWidgets import QMenu
        item = cat_list.itemAt(pos)
        menu = QMenu(cat_list)
        pid = item.data(Qt.ItemDataRole.UserRole) if item else None
        if pid:
            menu.addAction(
                "Insert into EDS",
                lambda: interop.shell_action(
                    parent, "profile.insert", profile_id=str(pid)))
            menu.addAction(
                "Insert missing only",
                lambda: _do_insert(
                    missing_entries(str(pid), session.draft_entries),
                    overwrite=False))
            menu.addAction("Open Dictionary",
                           lambda: interop.shell_action(parent, "view.eds"))
        else:
            menu.addAction(
                "Insert missing",
                lambda: _do_insert(
                    missing_entries(state["pid"], session.draft_entries),
                    overwrite=False))
            menu.addAction("Insert all visible", lambda: _do_insert(_visible()))
            menu.addAction("Use profile as new document", _use_as_new)
            menu.addSeparator()
            interop.add_bridge_actions(menu, parent, include_blank=True)
        menu.exec(cat_list.mapToGlobal(pos))

    def _tree_menu(pos):
        from PyQt6.QtWidgets import QMenu
        menu = QMenu(tree)
        item = tree.itemAt(pos)
        if item is None:
            interop.add_bridge_actions(
                menu, parent, profile_id=state.get("pid"), include_blank=True)
        else:
            e = item.data(0, Qt.ItemDataRole.UserRole)
            menu.addAction("Insert selection", lambda: _do_insert(_selected()))
            if e is not None:
                interop.add_bridge_actions(
                    menu, parent, index=e.index, subindex=e.subindex,
                    profile_id=state.get("pid"))
        menu.exec(tree.viewport().mapToGlobal(pos))

    cat_list.customContextMenuRequested.connect(_cat_menu)
    tree.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
    tree.customContextMenuRequested.connect(_tree_menu)

    for i in range(cat_list.count()):
        if cat_list.item(i).data(Qt.ItemDataRole.UserRole):
            cat_list.setCurrentRow(i)
            break

    root.select_library_view = select_library_view  # type: ignore[attr-defined]
    root.select_view = select_view  # type: ignore[attr-defined]
    return root
