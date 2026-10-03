# -*- coding: utf-8 -*-
"""Side Bar viewlet title must match VS Code (.part > .title)."""
from __future__ import annotations

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_SHARED = os.path.dirname(_HERE)
_PLUGINS = os.path.dirname(_SHARED)
if _PLUGINS not in sys.path:
    sys.path.insert(0, _PLUGINS)


def test_sidebar_title_tokens_and_stylesheet():
    from _shared import vscode_theme as T
    from _shared import suite_ui as sui

    assert sui.SIDEBAR_TITLE_H == 35
    ss = T.stylesheet()
    assert "QLabel#SuiteSideBarTitle" in ss
    assert "min-height: 35px" in ss
    i = ss.index("QWidget#SuiteSideBarHeader")
    block = ss[i : i + 220]
    # Viewlet title must not reuse pane-header 22px cap
    assert "max-height: 22px" not in block
    print("PASS sidebar title stylesheet")


def test_sidebar_header_runtime_font():
    from PyQt6.QtWidgets import QApplication, QLabel, QMainWindow

    from _shared import vscode_theme as T
    from _shared import suite_ui as sui

    app = QApplication.instance() or QApplication([])
    win = QMainWindow()
    T.apply(win)
    sui.apply_suite_chrome(win)
    head = sui.sidebar_header("Code")
    win.setCentralWidget(head)
    win.show()
    app.processEvents()
    lab = head.findChild(QLabel)
    assert lab is not None
    assert lab.objectName() == "SuiteSideBarTitle"
    assert lab.text() == "CODE"
    assert lab.font().pixelSize() == T.FS_META
    # setFixedHeight owns the row; allow 1px border/DPI slack
    assert abs(head.height() - sui.SIDEBAR_TITLE_H) <= 1
    assert lab.sizeHint().height() <= head.height()
    print("PASS sidebar header runtime font")


if __name__ == "__main__":
    test_sidebar_title_tokens_and_stylesheet()
    test_sidebar_header_runtime_font()
