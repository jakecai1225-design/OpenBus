# -*- coding: utf-8 -*-
"""VS Code–style SVG icons for domain suites (codicon look, currentColor tint)."""

from __future__ import annotations

import os
from functools import lru_cache

from PyQt6.QtCore import QByteArray, QSize, Qt
from PyQt6.QtGui import QIcon, QPainter, QPixmap
from PyQt6.QtSvg import QSvgRenderer
from PyQt6.QtWidgets import QAbstractButton, QListWidgetItem

_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

# Semantic aliases → file stem (no .svg)
ALIASES = {
    # UDS
    "setup": "settings",
    "diagnose": "beaker",
    "scan": "search",
    "batch": "checklist",
    "security": "lock",
    "profiles": "account",
    "log": "output",
    # DBC Studio
    "editor": "edit",
    "validate": "check",
    "compare": "list",
    "merge": "sync",
    "export": "export",
    "library": "database",
    # CANopen
    "network": "device",
    "monitor": "trace",
    "object_dict": "list",
    "pdo": "trace",
    "eds_editor": "file",
    "profiles": "account",
    # J1939 / OBD
    "analyzer": "search",
    "transport": "trace",
    "diagnostics": "beaker",
    "scanner": "search",
    "readiness": "check",
    # TX Lab
    "generator": "send",
    "restbus": "extensions",
    "replay": "record",
    "dashboard": "graphic",
    # Bus security
    "fuzzer": "beaker",
    "ids": "lock",
    "stress": "refresh",
    "e2e": "check",
    # Protocol hub
    "nm": "device",
    "isotp": "trace",
    "isobus": "device",
    "nmea2000": "device",
    "gbt27930": "device",
    "xcp": "trace",
    # Log analysis
    "toolkit": "folder",
    "trace": "trace",
    "report": "export",
    "findings": "list",
    "trigger": "record",
    "quality": "info",
    "reverse": "undo",
    "id_scan": "search",
    # Bus utilities
    "bit_timing": "settings",
    "gateway": "extensions",
    # AUTOSAR
    "system": "file",
    "com": "list",
    "secoc": "lock",
    # EtherCAT
    "topology": "device",
    "coe": "list",
    "esi": "file",
    "dc": "sync",
    "frames": "trace",
    "mailbox": "trace",
    # Controls
    "chevron-up": "chevron-up",
    "chevron-down": "chevron-down",
    "apply": "check",
    "send": "arrow-right",
    "browse": "folder",
    "start": "play",
    "stop": "stop",
    "clear": "clear-all",
    "add": "plus",
    "delete": "trash",
    "remove": "trash",
    "edit": "edit",
    "import": "folder",
    "save": "save",
    "load": "folder",
    "sync": "sync",
    "info": "info",
    "read": "info",
    "refresh": "refresh",
    "settings": "settings",
    "file": "file",
    "abort": "stop",
    "database": "database",
    "device": "device",
    "trace": "trace",
    "list": "list",
    "graphic": "graphic",
    "record": "record",
    "undo": "undo",
    "extensions": "extensions",
    # Workbench layout toggles (VS Code title-bar style)
    "layout-sidebar": "layout-sidebar-left",
    "layout-sidebar-left": "layout-sidebar-left",
    "layout-sidebar-right": "layout-sidebar-right",
    "layout-panel": "layout-panel",
    "layout-maximize": "layout-maximize",
}



def icon_path(name: str) -> str:
    stem = ALIASES.get(name, name)
    path = os.path.join(_DIR, stem + ".svg")
    if os.path.isfile(path):
        return path
    return os.path.join(_DIR, name + ".svg")


@lru_cache(maxsize=256)
def pixmap(name: str, color: str = "#333333", size: int = 16) -> QPixmap:
    path = icon_path(name)
    if not os.path.isfile(path):
        pm = QPixmap(size, size)
        pm.fill(Qt.GlobalColor.transparent)
        return pm
    with open(path, "r", encoding="utf-8") as f:
        svg = f.read()
    svg = svg.replace("currentColor", color)
    renderer = QSvgRenderer(QByteArray(svg.encode("utf-8")))
    pm = QPixmap(size, size)
    pm.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pm)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    renderer.render(painter)
    painter.end()
    return pm


def icon(name: str, color: str = "#333333", size: int = 16) -> QIcon:
    return QIcon(pixmap(name, color, size))


def set_button(
    btn: QAbstractButton,
    name: str,
    *,
    color: str = "#333333",
    size: int = 14,
    primary: bool = False,
) -> None:
    """Attach a VS Code icon to a push button (no character glyphs)."""
    c = "#FFFFFF" if primary else color
    btn.setIcon(icon(name, c, size))
    btn.setIconSize(QSize(size, size))


def set_nav_item(item: QListWidgetItem, name: str, color: str = "#333333") -> None:
    item.setIcon(icon(name, color, 16))
