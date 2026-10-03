# -*- coding: utf-8 -*-
"""Library — file starters + project templates + recent.

No tool_strip under Editor Tabs: pick a starter in the list, preview on the
right, then Use via next_step (or double-click). Matches Workspace empty_state
discipline (§4).
"""

from __future__ import annotations

import os

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog,
    QListWidget,
    QListWidgetItem,
    QSplitter,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from _shared import arxmlparse, suite_chrome
from pages import _ui


def _multi() -> arxmlparse.ArxmlModel:
    m = arxmlparse.ArxmlModel(package="BodyCan")
    m.ipdus = [
        arxmlparse.Ipdu("EngineData", 0x100, 8, [
            arxmlparse.Signal("RPM", 0, 16),
            arxmlparse.Signal("Temp", 16, 8, factor=1, offset=-40, unit="C"),
        ]),
        arxmlparse.Ipdu("Lights", 0x200, 2, [
            arxmlparse.Signal("Beam", 0, 2),
            arxmlparse.Signal("Turn", 2, 2),
        ]),
        arxmlparse.Ipdu("DoorStatus", 0x300, 1, [
            arxmlparse.Signal("Locked", 0, 1),
        ]),
    ]
    return m


def _empty() -> arxmlparse.ArxmlModel:
    return arxmlparse.ArxmlModel(package="OpenBus", ipdus=[])


FILE_STARTERS = [
    ("demo", "Demo body CAN (1 PDU)",
     "Single DemoPdu with one signal — smallest valid extract.",
     arxmlparse.empty_model),
    ("multi", "Multi-PDU sample",
     "Three PDUs with typical intel layout for learning Validate.",
     _multi),
    ("empty", "Empty package",
     "AR-PACKAGE only — add PDUs yourself.",
     _empty),
]

PROJECT_STARTERS = [
    ("proj_empty", "Project: empty ECU",
     "project.json + empty COM + derived ECUC stubs under work/."),
    ("proj_body", "Project: Body CAN COM+ECUC",
     "Body multi-PDU COM + full Com/CanIf/PduR/CanNm ECUC-lite."),
    ("proj_extract", "Project: Extract stub",
     "input/extract.arxml copy of COM + work intermediates."),
]


def build(shell, document, log_fn) -> QWidget:
    root = QWidget()
    layout = QVBoxLayout(root)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(0)

    split = QSplitter(Qt.Orientation.Horizontal)
    lst = QListWidget()
    lst.setObjectName("SuiteMatrix")
    for tid, title, blurb, _fn in FILE_STARTERS:
        item = QListWidgetItem(title)
        item.setData(Qt.ItemDataRole.UserRole, ("file", tid))
        item.setToolTip(blurb)
        lst.addItem(item)
    for tid, title, blurb in PROJECT_STARTERS:
        item = QListWidgetItem(title)
        item.setData(Qt.ItemDataRole.UserRole, ("project", tid))
        item.setToolTip(blurb)
        lst.addItem(item)
    split.addWidget(lst)

    right = QWidget()
    right.setObjectName("SuiteContent")
    rl = QVBoxLayout(right)
    suite_chrome.page_margins(rl)
    rl.setSpacing(10)

    use_btn = _ui.primary_btn(
        "Use starter", "Replace document or create project (or double-click)",
        "file")
    head, hl = _ui.content_header("Library")
    hl.addStretch(1)
    tip = _ui.quiet_label("Double-click a starter to apply")
    hl.addWidget(tip, 0, Qt.AlignmentFlag.AlignVCenter)
    rl.addWidget(head)

    blurb = QTextEdit()
    blurb.setReadOnly(True)
    blurb.setMaximumHeight(120)
    blurb.setObjectName("SuiteHintBox")
    recent = QListWidget()
    recent.setObjectName("SuiteMatrix")
    recent_proj = QListWidget()
    recent_proj.setObjectName("SuiteMatrix")
    rl.addWidget(_ui.section_title("Starter"))
    rl.addWidget(blurb)
    rl.addWidget(_ui.section_title("Recent files"))
    rl.addWidget(recent, 1)
    rl.addWidget(_ui.section_title("Recent projects"))
    rl.addWidget(recent_proj, 1)
    rl.addWidget(_ui.next_step_bar(
        "Apply the selected starter to the workspace.", use_btn))
    split.addWidget(right)
    layout.addWidget(split, 1)
    split.setStretchFactor(0, 2)
    split.setStretchFactor(1, 3)

    selected = {"kind": "file", "id": "demo"}

    def _blurb_for(kind, tid):
        if kind == "file":
            for row in FILE_STARTERS:
                if row[0] == tid:
                    return row[2]
        for row in PROJECT_STARTERS:
            if row[0] == tid:
                return row[1] + "\n\n" + row[2]
        return ""

    def _on_starter():
        item = lst.currentItem()
        if not item:
            return
        kind, tid = item.data(Qt.ItemDataRole.UserRole)
        selected["kind"] = kind
        selected["id"] = tid
        blurb.setPlainText(_blurb_for(kind, tid))

    def _use_file(tid):
        fn = next((r[3] for r in FILE_STARTERS if r[0] == tid), None)
        if not fn:
            return
        if document.dirty or document.path or document.has_project():
            if hasattr(shell, "_confirm_discard") and not shell._confirm_discard():
                return
        document.set_model(fn(), path="", dirty=True)
        document.ecuc = arxmlparse.empty_ecuc()
        document.manifest = None
        log_fn("OK", "Starter %s" % tid)
        shell.goto_page("editor")

    def _use_project(tid):
        if hasattr(shell, "_confirm_discard") and not shell._confirm_discard():
            return
        path = QFileDialog.getExistingDirectory(
            shell, "Folder for new project")
        if not path:
            return
        name = {
            "proj_empty": "EmptyEcu",
            "proj_body": "BodyEcu",
            "proj_extract": "ExtractEcu",
        }.get(tid, "UntitledEcu")
        seed = None
        if tid == "proj_empty":
            seed = _empty()
        elif tid in ("proj_body", "proj_extract"):
            seed = _multi()
        try:
            document.new_project(path, name=name, ecu_name=name, seed=seed)
            if tid == "proj_extract":
                extract = document.manifest.abs_role("extract")
                os.makedirs(os.path.dirname(extract), exist_ok=True)
                with open(extract, "w", encoding="utf-8") as f:
                    f.write(arxmlparse.serialize_model(document.model))
            if hasattr(shell, "_remember_project"):
                shell._remember_project(path)
            log_fn("OK", "Project starter %s → %s" % (tid, path))
            shell.goto_page("project")
        except OSError as e:
            log_fn("ERR", str(e))

    def _use():
        if selected["kind"] == "project":
            _use_project(selected["id"])
        else:
            _use_file(selected["id"])

    def _refresh_recent():
        recent.clear()
        for path in getattr(shell, "recent_files", lambda: [])():
            recent.addItem(QListWidgetItem(path))
        recent_proj.clear()
        for path in getattr(shell, "recent_projects", lambda: [])():
            recent_proj.addItem(QListWidgetItem(path))

    def _open_recent(item):
        path = item.text()
        if path and hasattr(shell, "_load_path"):
            if hasattr(shell, "_confirm_discard") and not shell._confirm_discard():
                return
            shell._load_path(path)

    def _open_recent_proj(item):
        path = item.text()
        if not path:
            return
        if hasattr(shell, "_confirm_discard") and not shell._confirm_discard():
            return
        try:
            document.open_project(path)
            if hasattr(shell, "_remember_project"):
                shell._remember_project(path)
            log_fn("OK", "Opened project %s" % path)
            shell.goto_page("project")
        except OSError as e:
            log_fn("ERR", str(e))

    lst.currentItemChanged.connect(lambda *_: _on_starter())
    lst.itemDoubleClicked.connect(lambda *_: _use())
    use_btn.clicked.connect(_use)
    recent.itemDoubleClicked.connect(_open_recent)
    recent_proj.itemDoubleClicked.connect(_open_recent_proj)
    document.on_changed(_refresh_recent)
    if lst.count():
        lst.setCurrentRow(0)
    _refresh_recent()
    return root
