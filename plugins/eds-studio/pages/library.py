# -*- coding: utf-8 -*-
"""Library — starter templates, CiA profiles, and PDO packs."""

from __future__ import annotations

from PyQt6.QtCore import Qt
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
    filt.setMaximumWidth(180)
    p_count = _ui.count_label()
    insert_all = _ui.ghost_btn("Insert all", "Merge every object in this list", "add")
    insert_sel = _ui.primary_btn(
        "Insert", "Merge selected objects into the open EDS", "add")
    p_crow.addWidget(filt)
    p_crow.addWidget(p_count)
    p_crow.addStretch(1)
    p_crow.addWidget(insert_all)
    p_crow.addWidget(insert_sel)
    pl.addWidget(p_chrome)

    p_split = QSplitter(Qt.Orientation.Horizontal)
    p_split.setHandleWidth(1)
    p_split.setChildrenCollapsible(False)

    cat_list = QListWidget()
    _ui.style_list(cat_list)
    cat_list.setMinimumWidth(220)
    cat_list.setMaximumWidth(300)
    for pid, title, kind, blurb in profiles.PROFILE_CATALOG:
        short = title
        if kind == "pack":
            short = title.replace("Pack — ", "")
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
    obj_tree.setHeaderLabels(["Index", "Name", "Access", "Default"])
    _ui.style_tree(obj_tree, stretch_col=1)
    obj_tree.setRootIsDecorated(False)
    obj_tree.setSelectionMode(QAbstractItemView.SelectionMode.ExtendedSelection)
    obj_tree.setToolTip("Double-click a row to insert it")
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
        document.upsert_entries(eds.entries, merge=True)
        cur = document.clone_eds()
        if not cur.device_info.get("ProductName"):
            cur.device_info.update(eds.device_info)
        if not cur.file_info.get("Description"):
            cur.file_info.update(eds.file_info)
        document.apply_eds(cur)
        log_fn("OK", "Merged starter %s" % tid)
        shell.goto_page("editor")

    def _refresh_objects():
        pid = selected_catalog["id"] or "301"
        meta = profiles.catalog_entry(pid)
        p_blurb.setText(meta[3] if meta else "")
        obj_tree.clear()
        rows = profiles.search_profile(filt.text(), pid)
        for e in rows:
            item = QTreeWidgetItem([
                e.display_index(), e.name, e.access_type or "",
                e.default_value or "",
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, e)
            obj_tree.addTopLevelItem(item)
        p_count.setText("%d objects" % len(rows))

    def _on_catalog():
        item = cat_list.currentItem()
        if not item:
            return
        selected_catalog["id"] = item.data(Qt.ItemDataRole.UserRole)
        _refresh_objects()

    def _insert(entries):
        if not entries:
            log_fn("WARN", "Nothing selected")
            return
        document.upsert_entries(entries, merge=True)
        log_fn("OK", "Inserted %d object(s)" % len(entries))
        shell.goto_page("editor")

    def _insert_selected():
        entries = []
        for item in obj_tree.selectedItems():
            e = item.data(0, Qt.ItemDataRole.UserRole)
            if e is not None:
                entries.append(e)
        _insert(entries)

    s_list.currentItemChanged.connect(lambda *_: _on_starter())
    s_list.itemDoubleClicked.connect(lambda *_: _use_starter())
    use_btn.clicked.connect(_use_starter)
    merge_btn.clicked.connect(_merge_starter)
    cat_list.currentItemChanged.connect(lambda *_: _on_catalog())
    filt.textChanged.connect(lambda *_: _refresh_objects())
    insert_sel.clicked.connect(_insert_selected)
    insert_all.clicked.connect(
        lambda: _insert(profiles.objects_for(selected_catalog["id"] or "301")))
    obj_tree.itemDoubleClicked.connect(
        lambda item, _c: _insert([item.data(0, Qt.ItemDataRole.UserRole)]))

    if s_list.count():
        s_list.setCurrentRow(0)
    if cat_list.count():
        cat_list.setCurrentRow(0)

    return root
