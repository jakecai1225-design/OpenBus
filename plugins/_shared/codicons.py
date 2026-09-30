# -*- coding: utf-8 -*-
"""VS Code–style SVG icons for domain suites (codicon look, currentColor tint)."""

from __future__ import annotations

import os
from functools import lru_cache
from typing import Optional

from PyQt6.QtCore import QByteArray, QRectF, QSize, Qt
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
    # DBC Studio activities — stems match NAV_PAGES keys (files must exist)
    "editor": "edit",
    "edit": "edit",
    "analyze": "analyze",
    "integrate": "integrate",
    "deliver": "deliver",
    "matrix": "list",
    "valuetables": "checklist",
    "attributes": "settings",
    "validate": "check",
    "timing": "trace",
    "compare": "search",
    "merge": "sync",
    "export": "export",
    "library": "database",    # CANopen Suite activity bar — distinct glyphs per workspace
    "network": "network",
    "monitor": "trace",
    "trace": "trace",
    "code": "play",
    "object_dict": "list",
    "od": "list",
    "pdo": "flow",
    "eds_editor": "eds",
    "eds": "eds",
    "library": "database",
    "profiles": "account",
    "device": "device",
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
    "project": "folder",
    "config": "gear",
    "bus": "device",
    "system": "file",
    "com": "list",
    "secoc": "lock",
    "bsw": "extensions",
    "spec": "info",
    "swc": "flow",
    "analysis": "trace",
    "findings": "check",
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
    "debug-stop": "stop",
    "debug-continue": "debug-continue",
    "continue": "debug-continue",
    "play": "play",
    "clear": "clear-all",
    "close": "close",
    "add": "plus",
    "plus": "plus",
    "delete": "trash",
    "remove": "trash",
    "trash": "trash",
    "edit": "edit",
    "import": "folder",
    "save": "save",
    "load": "folder",
    "sync": "sync",
    "refresh": "sync",
    "info": "info",
    "read": "info",
    "undo": "undo",
    "redo": "sync",
    "settings": "settings",
    "file": "file",
    "abort": "stop",
    "database": "database",
    "device": "device",
    "trace": "trace",
    "list": "list",
    "graphic": "graphic",
    "record": "record",
    "extensions": "extensions",
    "search": "search",
    "export": "export",
    "arrow-right": "arrow-right",
    "check": "check",
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
    # Fall back to the raw key so activity ids like analyze.svg always resolve
    # even when an older ALIASES map is cached / installed.
    direct = os.path.join(_DIR, name + ".svg")
    if os.path.isfile(direct):
        return direct
    return path


def _render_svg_file(
        path: str, size: int, *, tint: Optional[str] = None) -> QPixmap:
    """Rasterize an SVG at *size* with 3× supersample for sharp Win DPI edges."""
    if not os.path.isfile(path):
        pm = QPixmap(size, size)
        pm.fill(Qt.GlobalColor.transparent)
        return pm
    with open(path, "r", encoding="utf-8") as f:
        svg = f.read()
    if tint:
        svg = svg.replace("currentColor", tint)
    renderer = QSvgRenderer(QByteArray(svg.encode("utf-8")))
    scale = 3 if size <= 32 else (2 if size <= 64 else 1)
    raw = size * scale
    hi = QPixmap(raw, raw)
    hi.fill(Qt.GlobalColor.transparent)
    painter = QPainter(hi)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
    pad = max(scale, raw // 16)
    renderer.render(painter, QRectF(pad, pad, raw - 2 * pad, raw - 2 * pad))
    painter.end()
    if scale == 1:
        return hi
    return hi.scaled(
        size, size,
        Qt.AspectRatioMode.KeepAspectRatio,
        Qt.TransformationMode.SmoothTransformation)


@lru_cache(maxsize=512)
def pixmap(name: str, color: str = "#333333", size: int = 16) -> QPixmap:
    """Render a crisp codicon; 3× supersample for clear edges at toolbar sizes."""
    return _render_svg_file(icon_path(name), size, tint=color)


def clear_pixmap_cache() -> None:
    """Drop cached tinted SVGs (call after replacing icon files)."""
    pixmap.cache_clear()


def icon(name: str, color: str = "#333333", size: int = 16) -> QIcon:
    return QIcon(pixmap(name, color, size))


def window_icon_from_svg(path: str, sizes: tuple[int, ...] = (16, 24, 32, 48, 64)) -> QIcon:
    """Multi-resolution window / taskbar icon from a colored SVG (no tint)."""
    ic = QIcon()
    for s in sizes:
        pm = _render_svg_file(path, s, tint=None)
        if not pm.isNull():
            ic.addPixmap(pm)
    return ic


def set_button(
    btn: QAbstractButton,
    name: str,
    *,
    color: str = "#333333",
    size: int = 16,
    primary: bool = False,
) -> None:
    """Attach a VS Code icon; iconSize matches glyph so it never clips."""
    c = "#FFFFFF" if primary else color
    btn.setIcon(icon(name, c, size))
    btn.setIconSize(QSize(size, size))


def set_nav_item(item: QListWidgetItem, name: str, color: str = "#333333") -> None:
    item.setIcon(icon(name, color, 16))
