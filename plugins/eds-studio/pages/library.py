# -*- coding: utf-8 -*-
"""Library — starter templates, CiA profiles, and PDO packs.

Top1 (CANeds): browse standard profiles / packs and insert into OD.
Top2 (emotas): import missing-only or overwrite; jump to Editor after insert.
"""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QBrush, QColor
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMessageBox,
    QSplitter,
    QStackedWidget,
    QTabBar,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from _shared import suite_chrome
from _shared.canopen_profiles import catalog_by_category, present_keys
from core import profiles, templates
from pages import _ui


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.setToolTip("Starters = new document · Profiles = insert objects")
    bar.addTab("Starters")
    bar.addTab("Profiles")
    root.chrome_tabs = bar

    stack = QStackedWidget()
    bar.currentChanged.connect(stack.setCurrentIndex)
    layout.addWidget(stack, 1)

    selected_starter = {"id": None}
    selected_catalog = {"id": "301"}

    # ---- Starters ----
    starters = QWidget()
    sl = QVBoxLayout(starters)
    sl.setContentsMargins(0, 0, 0, 0)
    sl.setSpacing(0)

    s_chrome, s_crow = suite_chrome.make_toolbar()
    s_count = _ui.count_label()
    merge_btn = _ui.ghost_btn("Merge", "Add starter objects into the open file", "add")
    use_btn = _ui.primary_btn(
        "Use starter", "Replace document with this template", "file")
    s_crow.addWidget(s_count)
    s_crow.addStretch(1)
    s_crow.addWidget(merge_btn)
    s_crow.addWidget(use_btn)
    sl.addWidget(s_chrome)

    s_split = QSplitter(Qt.Orientation.Horizontal)
    s_split.setHandleWidth(1)
    s_split.setChildrenCollapsible(False)

    s_list = QListWidget()
    _ui.style_list(s_list)
    s_list.setMinimumWidth(220)
    s_list.setMaximumWidth(300)
    for tid, title, blurb, _fn in templates.STARTER_TEMPLATES:
        item = QListWidgetItem(title)
        item.setData(Qt.ItemDataRole.UserRole, tid)
        item.setToolTip(blurb + "\n\nDouble-click to use as new document.")
        s_list.addItem(item)
    s_split.addWidget(s_list)

    s_right = QWidget()
    s_right.setObjectName("SuiteContent")
    sr = QVBoxLayout(s_right)
    suite_chrome.page_margins(sr, top=10)
    sr.setSpacing(6)
    s_title = QLabel("Select a starter")
    s_title.setObjectName("SuiteSectionTitle")
    s_blurb = _ui.quiet_label("")
    s_preview = QTreeWidget()
    s_preview.setHeaderLabels(["Index", "Name", "Access"])
    _ui.style_tree(s_preview, stretch_col=1)
    s_preview.setRootIsDecorated(False)
    sr.addWidget(s_title)
    sr.addWidget(s_blurb)
    sr.addWidget(s_preview, 1)
    s_split.addWidget(s_right)
    s_split.setStretchFactor(0, 2)
    s_split.setStretchFactor(1, 5)
    sl.addWidget(s_split, 1)
    stack.addWidget(starters)

    # ---- Profiles ----
    profiles_page = QWidget()
    pl = QVBoxLayout(profiles_page)
    pl.setContentsMargins(0, 0, 0, 0)
    pl.setSpacing(0)

    p_chrome, p_crow = suite_chrome.make_toolbar()
    filt = _ui.line_edit("Filter objects in the selected profile", "Filter…")
    filt.setMaximumWidth(160)
    scope = _ui.combo(
        ["All objects", "Missing only", "Already in file"],
        "Compare profile against the open document (CANeds insert mode)")
    mode = _ui.combo(
        ["Overwrite existing", "Skip existing"],
        "When an index already exists: overwrite (default) or keep file value")
    p_count = _ui.count_label()
    insert_miss = _ui.ghost_btn(
        "Insert missing", "Import only objects not yet in the open EDS", "add")
    insert_all = _ui.ghost_btn("Insert all", "Merge every object in this list", "add")
    insert_sel = _ui.primary_btn(
        "Insert", "Merge selected objects into the open EDS", "add")
    p_crow.addWidget(filt)
    p_crow.addWidget(scope)
    p_crow.addWidget(mode)
    p_crow.addWidget(p_count)
    p_crow.addStretch(1)
    p_crow.addWidget(insert_miss)
    p_crow.addWidget(insert_all)
    p_crow.addWidget(insert_sel)
    pl.addWidget(p_chrome)

    p_split = QSplitter(Qt.Orientation.Horizontal)
    p_split.setHandleWidth(1)
    p_split.setChildrenCollapsible(False)

    cat_list = QListWidget()
    _ui.style_list(cat_list)
    cat_list.setMinimumWidth(240)
    cat_list.setMaximumWidth(320)
    # Grouped catalog: Communication / Device / Pack
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
    p_split.addWidget(cat_list)

    p_right = QWidget()
    p_right.setObjectName("SuiteContent")
    pr = QVBoxLayout(p_right)
    suite_chrome.page_margins(pr, top=10)
    pr.setSpacing(6)
    p_blurb = _ui.quiet_label("")
    obj_tree = QTreeWidget()
    obj_tree.setHeaderLabels(["Status", "Index", "Name", "Access", "Default"])
    _ui.style_tree(obj_tree, stretch_col=2)
    obj_tree.setRootIsDecorated(False)
    obj_tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    obj_tree.setToolTip(
        "Double-click to insert · green = already in file · blue = missing")
    pr.addWidget(p_blurb)
    pr.addWidget(obj_tree, 1)
    p_split.addWidget(p_right)
    p_split.setStretchFactor(0, 2)
    p_split.setStretchFactor(1, 5)
    pl.addWidget(p_split, 1)
    stack.addWidget(profiles_page)

    def _starter_meta(tid):
        for row in templates.STARTER_TEMPLATES:
            if row[0] == tid:
                return row
        return None

    def _on_starter():
        item = s_list.currentItem()
        if not item:
            return
        tid = item.data(Qt.ItemDataRole.UserRole)
        meta = _starter_meta(tid)
        if not meta:
            return
        selected_starter["id"] = tid
        _tid, title, blurb, fn = meta
        s_title.setText(title)
        s_blurb.setText(blurb)
        doc = fn()
        s_preview.clear()
        for e in doc.entries[:60]:
            s_preview.addTopLevelItem(QTreeWidgetItem([
                e.display_index(), e.name, e.access_type or ""]))
        more = len(doc.entries) - 60
        if more > 0:
            s_preview.addTopLevelItem(QTreeWidgetItem([
                "…", "+%d more" % more, ""]))
        s_count.setText("%d objects" % len(doc.entries))

    def _confirm_replace() -> bool:
        if not document.dirty and not document.path:
            return True
        if hasattr(shell, "_confirm_discard"):
            return shell._confirm_discard()
        ans = QMessageBox.question(
            shell, "Replace document",
            "Replace the open document with this starter?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        return ans == QMessageBox.StandardButton.Yes

    def _use_starter():
        tid = selected_starter["id"]
        if not tid:
            log_fn("WARN", "Pick a starter first")
            return
        if not _confirm_replace():
            return
        eds = templates.build_template(tid)
        document.set_eds(eds, path="", dirty=True)
        log_fn("OK", "Starter: %s (%d objects)" % (tid, len(eds.entries)))
        shell.goto_page("editor")

    def _merge_starter():
        tid = selected_starter["id"]
        if not tid:
            return
        eds = templates.build_template(tid)
        stats = document.upsert_entries(
            eds.entries, merge=True, overwrite=_overwrite())
        cur = document.clone_eds()
        if not cur.device_info.get("ProductName"):
            cur.device_info.update(eds.device_info)
        if not cur.file_info.get("Description"):
            cur.file_info.update(eds.file_info)
        document.apply_eds(cur)
        log_fn("OK", "Merged starter %s (+%d / ~%d / skip %d)" % (
            tid, stats["added"], stats["updated"], stats["skipped"]))
        shell.goto_page("editor")

    def _overwrite() -> bool:
        return mode.currentIndex() == 0

    def _doc_keys():
        return present_keys(document.eds.entries)

    def _refresh_objects():
        pid = selected_catalog["id"] or "301"
        meta = profiles.catalog_entry(pid)
        p_blurb.setText(meta[3] if meta else "")
        obj_tree.clear()
        rows = profiles.search_profile(filt.text(), pid)
        have = _doc_keys()
        scope_i = scope.currentIndex()
        shown = 0
        miss = 0
        for e in rows:
            in_doc = (e.index, e.subindex) in have
            if scope_i == 1 and in_doc:
                continue
            if scope_i == 2 and not in_doc:
                continue
            status = "in file" if in_doc else "missing"
            if not in_doc:
                miss += 1
            item = QTreeWidgetItem([
                status, e.display_index(), e.name,
                e.access_type or "", e.default_value or "",
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, e)
            if in_doc:
                item.setForeground(0, QBrush(QColor("#2E7D32")))
            else:
                item.setForeground(0, QBrush(QColor("#1565C0")))
            obj_tree.addTopLevelItem(item)
            shown += 1
        p_count.setText("%d shown · %d missing" % (shown, miss))

    def _on_catalog():
        item = cat_list.currentItem()
        if not item:
            return
        pid = item.data(Qt.ItemDataRole.UserRole)
        if not pid:
            return
        selected_catalog["id"] = pid
        _refresh_objects()

    def _insert(entries, force_overwrite=None):
        if not entries:
            log_fn("WARN", "Nothing selected")
            return
        ow = _overwrite() if force_overwrite is None else force_overwrite
        stats = document.upsert_entries(entries, merge=True, overwrite=ow)
        log_fn(
            "OK",
            "Imported %d object(s) (+%d new · ~%d overwrite · skip %d)"
            % (len(entries), stats["added"], stats["updated"], stats["skipped"]))
        _refresh_objects()
        shell.goto_page("editor")

    def _insert_selected():
        entries = []
        for item in obj_tree.selectedItems():
            e = item.data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                entries.append(e)
        _insert(entries)

    def _insert_all_visible():
        entries = []
        for i in range(obj_tree.topLevelItemCount()):
            e = obj_tree.topLevelItem(i).data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                entries.append(e)
        _insert(entries)

    def _insert_missing():
        pid = selected_catalog["id"] or "301"
        entries = profiles.missing_entries(pid, document.eds.entries)
        _insert(entries, force_overwrite=False)

    s_list.currentItemChanged.connect(lambda *_: _on_starter())
    s_list.itemDoubleClicked.connect(lambda *_: _use_starter())
    use_btn.clicked.connect(_use_starter)
    merge_btn.clicked.connect(_merge_starter)
    cat_list.currentItemChanged.connect(lambda *_: _on_catalog())
    filt.textChanged.connect(lambda *_: _refresh_objects())
    scope.currentIndexChanged.connect(lambda *_: _refresh_objects())
    insert_sel.clicked.connect(_insert_selected)
    insert_all.clicked.connect(_insert_all_visible)
    insert_miss.clicked.connect(_insert_missing)
    obj_tree.itemDoubleClicked.connect(
        lambda item, _c: _insert([item.data(0, Qt.ItemDataRole.UserRole)]))
    document.on_changed(lambda: _refresh_objects()
                        if stack.currentIndex() == 1 else None)

    if s_list.count():
        s_list.setCurrentRow(0)
    # Select first real catalog row (skip category header)
    for i in range(cat_list.count()):
        if cat_list.item(i).data(Qt.ItemDataRole.UserRole):
            cat_list.setCurrentRow(i)
            break

    return root
