# -*- coding: utf-8 -*-
"""Check — validate A2L references and addresses."""

from __future__ import annotations

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget

from _shared import a2lparse, plugin_shell
from pages import _ui


def build(parent, session, log_fn) -> QWidget:
    root = QWidget(parent)
    lay = QVBoxLayout(root)
    lay.setContentsMargins(0, 0, 0, 0)
    lay.setSpacing(0)

    run_btn = _ui.primary_btn("Check", "Validate the current A2L", "check")
    apply_btn = _ui.ghost_btn(
        "Apply → XCP", "Hand off after a clean check", "apply")
    status = _ui.muted_label("")
    lay.addWidget(_ui.tool_strip(run_btn, apply_btn, status, stretch_at=2))

    tree = QTreeWidget()
    tree.setHeaderLabels(["Severity", "Rule", "Message", "Symbol"])
    _ui.style_tree(tree, header_hidden=False)
    tree.setRootIsDecorated(False)
    lay.addWidget(tree, 1)

    def run_lint():
        findings = a2lparse.validate_document(session.doc)
        tree.clear()
        n_err = 0
        for f in findings:
            sev = f.get("severity", "info")
            if sev == "error":
                n_err += 1
            tree.addTopLevelItem(QTreeWidgetItem([
                sev, f.get("rule", ""), f.get("message", ""),
                f.get("name", ""),
            ]))
        session.mark_validated(n_err == 0)
        status.setText("%d finding(s), %d error(s)" % (len(findings), n_err))
        log_fn("SYS", "-", b"", "A2L check: %d errors" % n_err)
        plugin_shell.set_status(parent, status.text(), 3000)
        _ui.fit_columns(tree, stretch=2)

    def on_dbl(_item, _col):
        item = tree.currentItem()
        if item is None:
            return
        name = item.text(3)
        if name and hasattr(parent, "goto_page"):
            parent.goto_page("objects")

    run_btn.clicked.connect(run_lint)
    apply_btn.clicked.connect(
        lambda: parent.run_action("a2l.apply_xcp")
        if hasattr(parent, "run_action") else None)
    tree.itemDoubleClicked.connect(on_dbl)
    session.on_changed(lambda: status.setText(""))
    root.run_lint = run_lint  # type: ignore[attr-defined]
    return root
