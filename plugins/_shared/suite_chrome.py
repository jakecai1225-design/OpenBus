# -*- coding: utf-8 -*-
"""Shared suite chrome — same shell pattern as UDS Suite.

Use:
    from _shared import suite_chrome, vscode_theme, codicons
    vscode_theme.apply(self)
    nav, stack, content = suite_chrome.build_shell(self, NAV_PAGES)
"""

from __future__ import annotations

from PyQt6.QtCore import Qt, QSize
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QListWidget,
    QListWidgetItem,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from _shared import codicons, plugin_shell, vscode_theme


def build_shell(window, nav_pages: list[tuple[str, str]], *, nav_width: int = 148):
    """Attach themed nav + stack to window. Returns (nav, stack, page_index)."""
    vscode_theme.apply(window)
    plugin_shell.attach_status_bar(window, "Ready")

    central = QWidget()
    window.setCentralWidget(central)
    root = QHBoxLayout(central)
    root.setContentsMargins(0, 0, 0, 0)
    root.setSpacing(0)

    nav = QListWidget()
    nav.setObjectName("SuiteNav")
    nav.setFixedWidth(nav_width)
    nav.setIconSize(QSize(16, 16))
    nav.setFocusPolicy(Qt.FocusPolicy.NoFocus)
    nav.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    page_index = {}
    for i, (key, title) in enumerate(nav_pages):
        item = QListWidgetItem(title)
        item.setData(Qt.ItemDataRole.UserRole, key)
        codicons.set_nav_item(item, key, vscode_theme.TEXT)
        nav.addItem(item)
        page_index[key] = i
    root.addWidget(nav)

    content = QWidget()
    content.setObjectName("SuiteContent")
    cl = QVBoxLayout(content)
    cl.setContentsMargins(0, 0, 0, 0)
    cl.setSpacing(0)
    stack = QStackedWidget()
    cl.addWidget(stack, 1)
    root.addWidget(content, 1)

    return nav, stack, page_index


def page_margins(layout, *, top: int = 12) -> None:
    layout.setContentsMargins(16, top, 16, 12)
    layout.setSpacing(12)


def bind_nav_shortcuts(window, nav_pages, goto_fn) -> None:
    for i in range(len(nav_pages)):
        plugin_shell.bind_shortcut(
            window, "Ctrl+%d" % (i + 1),
            lambda _=False, idx=i: goto_fn(nav_pages[idx][0]))


def make_toolbar(title: str | None = None):
    """Flat bottom-border toolbar (replaces QGroupBox session strips)."""
    from PyQt6.QtWidgets import QLabel

    bar = QWidget()
    bar.setObjectName("SuiteToolbar")
    row = QHBoxLayout(bar)
    row.setContentsMargins(12, 6, 12, 6)
    row.setSpacing(8)
    if title:
        lab = QLabel(title.upper())
        lab.setObjectName("SuiteToolbarTitle")
        row.addWidget(lab)
    return bar, row


def make_log_host(title: str = "OUTPUT"):
    """Flat log host with muted title — no GroupBox frame."""
    from PyQt6.QtWidgets import QLabel

    host = QWidget()
    host.setObjectName("SuiteLogHost")
    v = QVBoxLayout(host)
    v.setContentsMargins(8, 6, 8, 8)
    v.setSpacing(6)
    title_lab = QLabel(title)
    title_lab.setObjectName("SuiteToolbarTitle")
    v.addWidget(title_lab)
    return host, v
