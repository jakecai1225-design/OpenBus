# -*- coding: utf-8 -*-
"""Cross-pane interop — MIME payloads, context menus, drag/drop helpers.

Right-click and drag connect EDS · Profiles · Device OD · Network without
forcing users through the activity bar alone.
"""

from __future__ import annotations

import json
from typing import Callable, Optional, Sequence

from PyQt6.QtCore import QByteArray, QMimeData, Qt, QPoint
from PyQt6.QtGui import QDrag
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QLabel,
    QListWidget,
    QMenu,
    QTreeWidget,
    QVBoxLayout,
    QWidget,
)

from pages import _ui

# Custom MIME types (Qt accepts application/x-*)
MIME_OD = "application/x-canopen-od"
MIME_PROFILE = "application/x-canopen-profile"
MIME_NODE = "application/x-canopen-node"


def od_payload(index: int, subindex: int = 0, name: str = "") -> bytes:
    return json.dumps({
        "index": int(index),
        "subindex": int(subindex),
        "name": name or "",
    }).encode("utf-8")


def parse_od_payload(raw: bytes) -> Optional[dict]:
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError):
        return None
    if not isinstance(data, dict) or "index" not in data:
        return None
    try:
        return {
            "index": int(data["index"]),
            "subindex": int(data.get("subindex") or 0),
            "name": str(data.get("name") or ""),
        }
    except (TypeError, ValueError):
        return None


def profile_payload(profile_id: str) -> bytes:
    return json.dumps({"profile_id": str(profile_id)}).encode("utf-8")


def parse_profile_payload(raw: bytes) -> Optional[str]:
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError):
        return None
    if isinstance(data, dict) and data.get("profile_id"):
        return str(data["profile_id"])
    return None


def node_payload(node_id: int) -> bytes:
    return json.dumps({"node_id": int(node_id)}).encode("utf-8")


def parse_node_payload(raw: bytes) -> Optional[int]:
    try:
        data = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError, TypeError):
        return None
    if isinstance(data, dict) and "node_id" in data:
        try:
            return max(1, min(127, int(data["node_id"])))
        except (TypeError, ValueError):
            return None
    return None


def mime_od(index: int, subindex: int = 0, name: str = "") -> QMimeData:
    md = QMimeData()
    md.setData(MIME_OD, QByteArray(od_payload(index, subindex, name)))
    md.setText("0x%04X:%02X" % (index, subindex))
    return md


def mime_profile(profile_id: str) -> QMimeData:
    md = QMimeData()
    md.setData(MIME_PROFILE, QByteArray(profile_payload(profile_id)))
    md.setText("profile:%s" % profile_id)
    return md


def mime_node(node_id: int) -> QMimeData:
    md = QMimeData()
    md.setData(MIME_NODE, QByteArray(node_payload(node_id)))
    md.setText("node:%d" % node_id)
    return md


def shell_action(shell, name: str, **kw) -> None:
    if shell is None:
        return
    if hasattr(shell, "run_action"):
        shell.run_action(name, **kw)
    elif name == "view.eds" and hasattr(shell, "goto_page"):
        shell.goto_page("eds_dict")
    elif name == "view.od" and hasattr(shell, "goto_page"):
        shell.goto_page("od")


def add_bridge_actions(
        menu: QMenu, shell, *,
        index: Optional[int] = None,
        subindex: int = 0,
        profile_id: Optional[str] = None,
        node_id: Optional[int] = None,
        include_blank: bool = False) -> None:
    """Cross-pane actions shared by context menus."""
    menu.addSeparator()
    if index is not None:
        menu.addAction(
            "Edit in EDS Dictionary",
            lambda: shell_action(
                shell, "eds.focus", index=index, subindex=subindex))
        menu.addAction(
            "Read on Device (SDO)",
            lambda: shell_action(
                shell, "device.focus_read", index=index, subindex=subindex))
    if profile_id:
        menu.addAction(
            "Insert profile into EDS",
            lambda: shell_action(
                shell, "profile.insert", profile_id=profile_id))
        menu.addAction(
            "Open Profiles panel",
            lambda: shell_action(shell, "profile.browse"))
    if node_id is not None:
        menu.addAction(
            "Use as Node-ID + open OD",
            lambda: shell_action(
                shell, "network.use_node", node_id=node_id))
    if include_blank:
        menu.addAction(
            "Apply EDS → OD",
            lambda: shell_action(shell, "eds.apply_od"))
        menu.addAction(
            "Browse Profiles…",
            lambda: shell_action(shell, "profile.browse"))
        menu.addAction(
            "Open Dictionary",
            lambda: shell_action(shell, "view.eds"))
        menu.addAction(
            "Open Live OD",
            lambda: shell_action(shell, "view.od"))
        menu.addAction(
            "Scan network",
            lambda: shell_action(shell, "network.scan"))


def enable_tree_drag(tree: QTreeWidget) -> None:
    tree.setDragEnabled(True)
    tree.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly)
    tree.setDefaultDropAction(Qt.DropAction.CopyAction)


def enable_tree_drop(tree: QTreeWidget) -> None:
    tree.setAcceptDrops(True)
    tree.setDragDropMode(QAbstractItemView.DragDropMode.DropOnly)
    tree.viewport().setAcceptDrops(True)


class DropZone(QWidget):
    """Blank-area drop target with a short hint label."""

    def __init__(
            self, shell, *,
            title: str = "Drop here",
            hint: str = "",
            accept_od: bool = True,
            accept_profile: bool = True,
            accept_node: bool = True,
            on_od: Optional[Callable[[dict], None]] = None,
            on_profile: Optional[Callable[[str], None]] = None,
            on_node: Optional[Callable[[int], None]] = None,
            compact: bool = False,
            parent=None):
        super().__init__(parent)
        self._shell = shell
        self._accept_od = accept_od
        self._accept_profile = accept_profile
        self._accept_node = accept_node
        self._on_od = on_od
        self._on_profile = on_profile
        self._on_node = on_node
        self.setObjectName("SuiteDropZone")
        self.setAcceptDrops(True)
        if compact:
            self.setMinimumHeight(28)
            self.setMaximumHeight(36)
            pad = 2
        else:
            self.setMinimumHeight(40)
            pad = 6
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._blank_menu)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(_ui.PAD_X, pad, _ui.PAD_X, pad)
        lay.setSpacing(0)
        t = QLabel(title)
        t.setObjectName("SuiteCount")
        t.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(t)
        if hint and not compact:
            h = _ui.quiet_label(hint)
            h.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lay.addWidget(h)

    def _blank_menu(self, pos: QPoint):
        menu = QMenu(self)
        add_bridge_actions(menu, self._shell, include_blank=True)
        menu.exec(self.mapToGlobal(pos))

    def dragEnterEvent(self, event):
        md = event.mimeData()
        if md is None:
            return
        if self._accept_od and md.hasFormat(MIME_OD):
            event.acceptProposedAction()
            return
        if self._accept_profile and md.hasFormat(MIME_PROFILE):
            event.acceptProposedAction()
            return
        if self._accept_node and md.hasFormat(MIME_NODE):
            event.acceptProposedAction()
            return
        event.ignore()

    def dropEvent(self, event):
        md = event.mimeData()
        if md is None:
            return
        if self._accept_od and md.hasFormat(MIME_OD):
            raw = bytes(md.data(MIME_OD))
            payload = parse_od_payload(raw)
            if payload:
                if self._on_od:
                    self._on_od(payload)
                else:
                    shell_action(
                        self._shell, "eds.focus",
                        index=payload["index"],
                        subindex=payload["subindex"])
                event.acceptProposedAction()
                return
        if self._accept_profile and md.hasFormat(MIME_PROFILE):
            pid = parse_profile_payload(bytes(md.data(MIME_PROFILE)))
            if pid:
                if self._on_profile:
                    self._on_profile(pid)
                else:
                    shell_action(
                        self._shell, "profile.insert", profile_id=pid)
                event.acceptProposedAction()
                return
        if self._accept_node and md.hasFormat(MIME_NODE):
            nid = parse_node_payload(bytes(md.data(MIME_NODE)))
            if nid is not None:
                if self._on_node:
                    self._on_node(nid)
                else:
                    shell_action(
                        self._shell, "network.use_node", node_id=nid)
                event.acceptProposedAction()
                return
        event.ignore()


def start_od_drag(widget: QWidget, index: int, subindex: int = 0,
                  name: str = "") -> None:
    drag = QDrag(widget)
    drag.setMimeData(mime_od(index, subindex, name))
    drag.exec(Qt.DropAction.CopyAction)


def start_profile_drag(widget: QWidget, profile_id: str) -> None:
    drag = QDrag(widget)
    drag.setMimeData(mime_profile(profile_id))
    drag.exec(Qt.DropAction.CopyAction)


def start_node_drag(widget: QWidget, node_id: int) -> None:
    drag = QDrag(widget)
    drag.setMimeData(mime_node(node_id))
    drag.exec(Qt.DropAction.CopyAction)


def _od_key_from_item(item) -> Optional[tuple]:
    if item is None:
        return None
    data = item.data(0, Qt.ItemDataRole.UserRole)
    if isinstance(data, tuple) and len(data) >= 2:
        try:
            return (int(data[0]), int(data[1]))
        except (TypeError, ValueError):
            return None
    if isinstance(data, int):
        return (int(data), 0)
    return None


class OdObjectTree(QTreeWidget):
    """EDS / OD tree that drags application/x-canopen-od payloads."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragDrop)
        self.setDefaultDropAction(Qt.DropAction.CopyAction)
        self.setAcceptDrops(True)
        self._on_profile_drop: Optional[Callable[[str], None]] = None
        self._on_node_drop: Optional[Callable[[int], None]] = None

    def set_profile_drop_handler(self, fn: Callable[[str], None]):
        self._on_profile_drop = fn

    def set_node_drop_handler(self, fn: Callable[[int], None]):
        self._on_node_drop = fn

    def startDrag(self, supportedActions):
        item = self.currentItem()
        key = _od_key_from_item(item)
        if not key:
            return
        name = item.text(1) if item.columnCount() > 1 else item.text(0)
        drag = QDrag(self)
        drag.setMimeData(mime_od(key[0], key[1], name))
        drag.exec(Qt.DropAction.CopyAction)

    def dragEnterEvent(self, event):
        md = event.mimeData()
        if md and (md.hasFormat(MIME_PROFILE) or md.hasFormat(MIME_NODE)
                   or md.hasFormat(MIME_OD)):
            event.acceptProposedAction()
        else:
            event.ignore()

    def dragMoveEvent(self, event):
        self.dragEnterEvent(event)

    def dropEvent(self, event):
        md = event.mimeData()
        if md is None:
            event.ignore()
            return
        if md.hasFormat(MIME_PROFILE) and self._on_profile_drop:
            pid = parse_profile_payload(bytes(md.data(MIME_PROFILE)))
            if pid:
                self._on_profile_drop(pid)
                event.acceptProposedAction()
                return
        if md.hasFormat(MIME_NODE) and self._on_node_drop:
            nid = parse_node_payload(bytes(md.data(MIME_NODE)))
            if nid is not None:
                self._on_node_drop(nid)
                event.acceptProposedAction()
                return
        event.ignore()


class ProfileCatalogList(QListWidget):
    """Profile catalog that drags application/x-canopen-profile."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly)

    def startDrag(self, supportedActions):
        item = self.currentItem()
        if item is None:
            return
        pid = item.data(Qt.ItemDataRole.UserRole)
        if not pid:
            return
        drag = QDrag(self)
        drag.setMimeData(mime_profile(str(pid)))
        drag.exec(Qt.DropAction.CopyAction)


class NodeScanTree(QTreeWidget):
    """Network scan results — drag node-id payloads."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setDragEnabled(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.DragOnly)

    def startDrag(self, supportedActions):
        item = self.currentItem()
        if item is None:
            return
        node = item.data(0, Qt.ItemDataRole.UserRole)
        if node is None:
            try:
                node = int(item.text(0))
            except ValueError:
                return
        drag = QDrag(self)
        drag.setMimeData(mime_node(int(node)))
        drag.exec(Qt.DropAction.CopyAction)


# Style hook for drop zones (applied via _ui overlay when needed)
DROP_ZONE_CSS = """
QWidget#SuiteDropZone {
  border: 1px dashed #5A5A5A;
  border-radius: 4px;
  background: transparent;
}
QWidget#SuiteDropZone:hover {
  border-color: #0E639C;
}
"""
