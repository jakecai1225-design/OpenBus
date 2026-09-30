# -*- coding: utf-8 -*-
"""Generate plugins/_shared/suite_ui.py from canopen pages/_ui.py."""

from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "plugins" / "canopen-suite" / "pages" / "_ui.py"
DST = ROOT / "plugins" / "_shared" / "suite_ui.py"

HEADER = '''# -*- coding: utf-8 -*-
"""Shared suite UI kit - industrial IDE density (VS Code Light).

Chrome recipe (every work page):
  1. tool_strip or panel_header - one TOOL_H row (CTRL_H + pad)
  2. optional inline_filter - same height so fields keep all four borders
  3. work surface (tree / table / form) - stretch
Hints go in tooltips, never a second caption row under the chrome.
"""
'''


def main() -> None:
    src = SRC.read_text(encoding="utf-8")
    idx = src.find("from __future__")
    if idx < 0:
        raise SystemExit("missing future import")
    body = src[idx:]
    body = body.replace("CANOPEN_OVERLAY", "SUITE_OVERLAY")
    body = body.replace("apply_canopen_chrome", "apply_suite_chrome")
    body = body.replace(
        "CANopen industrial overlay", "Shared suite industrial overlay")
    # Keep ASCII comments only in header; body may retain English en-dashes
    out = HEADER + "\n" + body
    # Append polish helpers if not already present
    if "def polish_work_surface" not in out:
        out += POLISH
    if "def style_page_tabs" not in out:
        # insert before polish or append
        out += TABS
    DST.write_text(out, encoding="utf-8", newline="\n")
    print("wrote", DST, "bytes", DST.stat().st_size)


TABS = '''

def style_page_tabs(tabs) -> None:
    """Underline-style page tabs (documentMode), shared across suites."""
    from PyQt6.QtWidgets import QTabWidget
    if not isinstance(tabs, QTabWidget):
        return
    tabs.setObjectName("SuitePageTabs")
    tabs.setDocumentMode(True)
    tabs.setMovable(False)
    bar = tabs.tabBar()
    if bar is not None:
        bar.setObjectName("SuiteTopTabs")
        bar.setExpanding(False)
        bar.setDrawBase(False)
'''

POLISH = '''

def muted_label(text: str = "") -> QLabel:
    """Status / path muted text using theme tokens (no hardcoded hex)."""
    lab = QLabel(text)
    lab.setObjectName("SuiteDocPath")
    return lab


def polish_work_surface(root: QWidget) -> None:
    """Unify ad-hoc widgets under a work page to suite chrome.

    - Bare QPushButton -> GhostButton @ CTRL_H
    - Bare QTreeWidget / QTableWidget -> SuiteMatrix styling
    - Bare QTabWidget -> SuitePageTabs
    Does not rewrite PrimaryButton / already-named widgets.
    """
    from PyQt6.QtWidgets import QPushButton, QTabWidget, QTableWidget, QTreeWidget
    if root is None:
        return
    for btn in root.findChildren(QPushButton):
        name = btn.objectName() or ""
        if name in ("PrimaryButton", "GhostButton", "SuiteTabClose"):
            continue
        if not name:
            btn.setObjectName("GhostButton")
        try:
            btn.setFixedHeight(CTRL_H)
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
        except Exception:
            pass
    for tree in root.findChildren(QTreeWidget):
        if tree.objectName() != "SuiteMatrix":
            style_tree(tree, header_hidden=tree.isHeaderHidden())
    for table in root.findChildren(QTableWidget):
        if table.objectName() != "SuiteMatrix":
            style_table(table)
    for tabs in root.findChildren(QTabWidget):
        if tabs.objectName() not in ("SuitePageTabs",):
            style_page_tabs(tabs)
'''


if __name__ == "__main__":
    main()
