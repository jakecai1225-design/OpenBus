# -*- coding: utf-8 -*-
"""Shared closable editor-tab strip (VS Code style) for domain suites.

Suites keep ``_open_tabs`` / ``_active_feature`` and call these helpers.
Requires ``shell._wb.set_editor_tabs`` from suite_chrome.
"""

from __future__ import annotations

from typing import Callable, Optional, Sequence

from PyQt6.QtCore import QSize, Qt
from PyQt6.QtWidgets import QHBoxLayout, QTabBar, QToolButton, QWidget

from _shared import codicons, vscode_theme


def normalize_open_tabs(
        raw,
        *,
        titles: dict,
        routes: dict,
        activity_keys: frozenset,
        aliases: Optional[dict] = None,
) -> list[str]:
    """Keep known leaf feature keys only (drop pure activity keys / junk)."""
    aliases = aliases or {}
    restored: list[str] = []
    items: list[str] = []
    if isinstance(raw, list):
        items = [x for x in raw if isinstance(x, str)]
    elif isinstance(raw, dict):
        for _ws, v in raw.items():
            if not isinstance(v, list):
                continue
            for x in v:
                if isinstance(x, str) and x not in items:
                    items.append(x)
    for x in items:
        key = aliases.get(x, x)
        if is_leaf_feature(key, titles=titles, routes=routes):
            if key not in restored:
                restored.append(key)
            continue
        # Pure activity keys (in activity_keys but not titles) are dropped.
        if key in activity_keys:
            continue
        if key not in routes or key not in titles:
            continue
        if key not in restored:
            restored.append(key)
    return restored


def is_leaf_feature(feature: str, *, titles: dict, routes: dict) -> bool:
    """True if ``feature`` may open as an editor tab.

    Activity ids that collide with leaf keys (scan/batch/…) are allowed when
    they appear in ``titles``. Pure activities (diagnose) are not.
    """
    return bool(feature) and feature in titles and feature in routes


def mount_editor_tabs(
        shell,
        *,
        open_tabs: Sequence[str],
        active_feature: str,
        titles: dict,
        on_activate: Callable[[str], None],
        on_close: Callable[[str], None],
        on_reorder: Optional[Callable[[list], None]] = None,
        ensure_status: Optional[Callable[[], None]] = None,
) -> QTabBar:
    """Build SuiteEditorTabs host and attach via ``shell._wb.set_editor_tabs``."""
    opens = list(open_tabs)
    bar = QTabBar()
    bar.setObjectName("SuiteEditorTabs")
    bar.setDrawBase(False)
    bar.setExpanding(False)
    bar.setDocumentMode(True)
    bar.setTabsClosable(False)
    bar.setMovable(True)
    guard = {"on": False}

    for feat in opens:
        idx = bar.addTab(titles.get(feat, feat))
        bar.setTabToolTip(idx, titles.get(feat, feat))
        bar.setTabData(idx, feat)
        close_btn = QToolButton(bar)
        close_btn.setObjectName("SuiteTabClose")
        close_btn.setAutoRaise(True)
        close_btn.setFixedSize(18, 18)
        close_btn.setIconSize(QSize(12, 12))
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setToolTip("Close tab")
        close_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        codicons.set_button(
            close_btn, "close", color=vscode_theme.TEXT_DIM, size=12)
        bar.setTabButton(idx, QTabBar.ButtonPosition.RightSide, close_btn)

        def _close_feat(_checked=False, key=feat):
            for j in range(bar.count()):
                if bar.tabData(j) == key:
                    bar.tabCloseRequested.emit(j)
                    break

        close_btn.clicked.connect(_close_feat)

    if active_feature in opens:
        bar.setCurrentIndex(opens.index(active_feature))
    elif opens:
        bar.setCurrentIndex(len(opens) - 1)

    def _changed(idx: int):
        if guard["on"] or idx < 0:
            return
        feat = bar.tabData(idx)
        if feat:
            on_activate(str(feat))

    def _close(idx: int):
        if idx < 0 or idx >= bar.count():
            return
        feat = bar.tabData(idx)
        if feat:
            on_close(str(feat))

    def _moved(_from: int, _to: int):
        order = []
        for i in range(bar.count()):
            d = bar.tabData(i)
            if d:
                order.append(str(d))
        if order and on_reorder:
            on_reorder(order)

    bar.currentChanged.connect(_changed)
    bar.tabCloseRequested.connect(_close)
    bar.tabMoved.connect(_moved)

    host = QWidget()
    host.setObjectName("SuiteEditorTabHost")
    row = QHBoxLayout(host)
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(0)
    row.addWidget(bar, 1)
    shell._wb.set_editor_tabs(host)
    if ensure_status:
        ensure_status()
    shell._tab_bar = bar
    shell._tab_guard = guard
    return bar
