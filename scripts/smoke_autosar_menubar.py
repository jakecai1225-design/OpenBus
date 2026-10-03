# -*- coding: utf-8 -*-
"""Offscreen check: SuiteMenuChrome stays visible after editor tab mount."""
from __future__ import annotations

import os
import pathlib
import sys
import types

ROOT = pathlib.Path(__file__).resolve().parents[1]
PLUGINS = ROOT / "plugins"
sys.path.insert(0, str(PLUGINS / "autosar-suite"))
sys.path.insert(0, str(PLUGINS))
sys.path.insert(0, str(ROOT / "build" / "bin" / "sdk"))

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")


def main() -> int:
    from PyQt6.QtWidgets import QApplication, QMenuBar

    app = QApplication.instance() or QApplication([])

    # Pin suite widgets ahead of _shared (same as main.py).
    suite_widgets = str(PLUGINS / "autosar-suite" / "widgets")
    if suite_widgets not in sys.path:
        sys.path.insert(0, suite_widgets)

    from app_shell import AppShell

    ctx = types.SimpleNamespace(
        register_command=lambda *a, **k: None,
        plugin_id="autosar-suite",
    )
    win = AppShell(ctx)
    win.resize(1280, 800)
    win.show()
    app.processEvents()

    # Open a leaf tab (triggers _mount_tab_bar).
    open_tab = getattr(win, "_open_feature_tab", None)
    if callable(open_tab):
        open_tab("bsw", activate=True)
    app.processEvents()
    win.showMaximized()
    app.processEvents()

    chrome = getattr(win, "_suite_menu_chrome", None)
    slot = getattr(win, "_suite_menu_chrome_slot", None)
    buttons = getattr(win, "_suite_menu_buttons", None)
    trailing = getattr(win, "_menubar_trailing", None)

    mw = win.menuWidget()
    mb = win.menuBar()

    def _geo(w):
        if w is None:
            return "None"
        r = w.geometry()
        return (
            "vis=%s size=%dx%d pos=%d,%d obj=%s parent=%s"
            % (
                w.isVisible(),
                r.width(),
                r.height(),
                r.x(),
                r.y(),
                w.objectName(),
                w.parentWidget().objectName() if w.parentWidget() else None,
            ))

    print("chrome", _geo(chrome))
    print("slot", _geo(slot))
    print("buttons", _geo(buttons))
    print("trailing", _geo(trailing))
    print("menuWidget", _geo(mw))
    print(
        "menuBar",
        _geo(mb),
        "actions",
        mb.actions().__len__() if isinstance(mb, QMenuBar) else "n/a",
    )

    btn_count = 0
    labels = []
    widths = []
    push_menu_btns = 0
    narrow = []
    if buttons is not None:
        from PyQt6.QtGui import QFontMetrics
        from PyQt6.QtWidgets import QPushButton
        from _shared.suite_chrome import SuiteMenuButton
        for b in buttons.findChildren(SuiteMenuButton):
            btn_count += 1
            labels.append(b.text())
            widths.append(b.width())
            need = QFontMetrics(b.font()).horizontalAdvance(b.text()) + 16
            if b.width() < need:
                narrow.append((b.text(), b.width(), need))
            if not isinstance(b, type) and b.__class__.__name__ != "SuiteMenuButton":
                pass
            # Must be QLabel subclass, not QPushButton.
            from PyQt6.QtWidgets import QLabel
            if not isinstance(b, QLabel) or isinstance(b, QPushButton):
                print("BAD_TYPE", type(b))
                narrow.append(("TYPE", 0, 1))
        push_menu_btns = len([
            t for t in buttons.findChildren(QPushButton)
            if t.objectName() in ("SuiteMenuButton", "SuiteMenuLabel")])
    print("menu_btn_count", btn_count, "labels", labels, "widths", widths,
          "push_menu_btns", push_menu_btns)
    if narrow:
        print("NARROW", narrow)

    # Occlusion check: harvested QMenus must NOT be visible children.
    from PyQt6.QtWidgets import QMenu
    visible_menus = []
    for m in win.findChildren(QMenu):
        if m.isVisible() and m.objectName() != "ignore":
            # Popups that are open shouldn't exist before click.
            parent_name = (
                m.parentWidget().objectName()
                if m.parentWidget() is not None else "")
            visible_menus.append(
                (m.title(), m.geometry().width(), m.geometry().height(),
                 parent_name))
    print("visible_menus", visible_menus)

    from PyQt6.QtCore import QPoint
    hit = win.childAt(QPoint(80, 12))
    hit_name = ""
    if hit is not None:
        hit_name = hit.objectName() or hit.__class__.__name__
    print("childAt(80,12)", hit_name)
    hit_ok = hit_name in ("SuiteMenuLabel", "SuiteMenuButtons", "SuiteMenuChrome")

    expected = ["File", "Edit", "View", "Run", "Help"]
    labels_ok = labels == expected
    widths_ok = (not narrow) and all(w >= 50 for w in widths)
    sentinel_ok = (
        mb is not None
        and mb.objectName() == "SuiteMenuBarSentinel"
        and (not mb.isVisible())
    )
    ok = (
        chrome is not None
        and chrome.isVisible()
        and chrome.height() >= 34
        and labels_ok
        and widths_ok
        and push_menu_btns == 0
        and not visible_menus
        and hit_ok
        and slot is not None
        and slot.height() >= 34
        and sentinel_ok
    )
    if not labels_ok:
        print("expected labels", expected)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
