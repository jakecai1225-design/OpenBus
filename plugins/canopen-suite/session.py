# -*- coding: utf-8 -*-
"""SharedSession — node, EDS OD, SDO client, NMT helpers for CANopen Suite."""

from __future__ import annotations

import copy
from typing import Callable, List, Optional

import sin
from PyQt6.QtCore import QObject

from core.eds_parse import OdEntry, parse_eds_file_document
from core.sdo_client import SdoClient


NMT_START = 0x01
NMT_STOP = 0x02
NMT_PREOP = 0x80
NMT_RESET_NODE = 0x81
NMT_RESET_COMM = 0x82

NMT_CMD_NAMES = {
    NMT_START: "Start",
    NMT_STOP: "Stop",
    NMT_PREOP: "Enter Pre-op",
    NMT_RESET_NODE: "Reset Node",
    NMT_RESET_COMM: "Reset Communication",
}


class SharedSession(QObject):
    """Suite-wide CANopen state: selected node, EDS path/entries, SDO client."""

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.node_id = 1
        self.eds_path = ""
        self.od_entries: List[OdEntry] = []
        self.draft_entries: List[OdEntry] = []  # EDS editor working copy
        self.eds_file_info: dict = {}
        self.eds_device_info: dict = {}
        self.eds_other_meta: dict = {}
        self.bitrate_hint = "(bus bitrate from host)"

        self._node_listeners: list[Callable[[], None]] = []
        self._od_listeners: list[Callable[[], None]] = []
        self._log_fn: Optional[Callable] = None
        self._frame_listeners: list[Callable] = []

        def _send(can_id, data):
            sin.frames.send(can_id, data)

        self.sdo = SdoClient(_send, parent=self, timeout_ms=800)
        self.sdo.set_node(self.node_id)
        self.sdo.on_log = self._sdo_log

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def _sdo_log(self, direction, can_id, pdu, note):
        self.log(direction, can_id, pdu, note)

    def on_node_changed(self, cb: Callable[[], None]) -> None:
        self._node_listeners.append(cb)

    def on_od_changed(self, cb: Callable[[], None]) -> None:
        self._od_listeners.append(cb)

    def on_bus_frame(self, cb: Callable) -> None:
        """Extra listeners for Monitor / Network (after SDO handling)."""
        self._frame_listeners.append(cb)

    def _notify_node(self) -> None:
        for cb in list(self._node_listeners):
            try:
                cb()
            except Exception:
                pass

    def _notify_od(self) -> None:
        for cb in list(self._od_listeners):
            try:
                cb()
            except Exception:
                pass

    def set_node_id(self, node_id: int) -> None:
        self.node_id = max(1, min(127, int(node_id)))
        self.sdo.set_node(self.node_id)
        self._notify_node()
        self.log("RX", "-", b"", "Node-ID set to %d" % self.node_id)

    def load_eds(self, path: str) -> bool:
        try:
            doc = parse_eds_file_document(path)
        except OSError as e:
            self.log("ERR", "-", b"", "EDS open failed: %s" % e)
            return False
        self.eds_path = path
        self.od_entries = doc.entries
        self.draft_entries = copy.deepcopy(doc.entries)
        self.eds_file_info = dict(doc.file_info)
        self.eds_device_info = dict(doc.device_info)
        self.eds_other_meta = {k: dict(v) for k, v in doc.other_meta.items()}
        self._notify_od()
        self.log(
            "RX", "-", b"",
            "Loaded EDS: %s (%d objects)" % (path, len(doc.entries)))
        return True

    def clear_eds(self) -> None:
        self.eds_path = ""
        self.od_entries = []
        self.draft_entries = []
        self.eds_file_info = {}
        self.eds_device_info = {}
        self.eds_other_meta = {}
        self._notify_od()
        self.log("RX", "-", b"", "EDS cleared")

    def set_draft_from_library(
            self, entries: List[OdEntry], merge: bool = True,
            overwrite: bool = True) -> dict:
        """Insert library stubs into EDS editor draft.

        Returns counts: added / updated / skipped.
        """
        stats = {"added": 0, "updated": 0, "skipped": 0}
        if not merge:
            self.draft_entries = copy.deepcopy(entries)
            stats["added"] = len(entries)
        else:
            by_key = {(e.index, e.subindex): e for e in self.draft_entries}
            for e in entries:
                key = (e.index, e.subindex)
                if key in by_key:
                    if overwrite:
                        by_key[key] = copy.deepcopy(e)
                        stats["updated"] += 1
                    else:
                        stats["skipped"] += 1
                else:
                    by_key[key] = copy.deepcopy(e)
                    stats["added"] += 1
            self.draft_entries = sorted(
                by_key.values(), key=lambda x: (x.index, x.subindex))
        self._notify_od()
        return stats

    def sync_od_from_draft(self) -> None:
        self.od_entries = copy.deepcopy(self.draft_entries)
        self._notify_od()

    def send_nmt(self, command: int, node_id: Optional[int] = None) -> None:
        nid = self.node_id if node_id is None else int(node_id)
        # 0 = all nodes
        nid = max(0, min(127, nid))
        pdu = bytes([command & 0xFF, nid & 0xFF])
        sin.frames.send(0x000, pdu)
        name = NMT_CMD_NAMES.get(command, "0x%02X" % command)
        self.log("TX", 0x000, pdu, "NMT %s → node %d" % (name, nid))

    def on_frame(self, frame) -> None:
        data = bytes(frame.data) if frame.data else b""
        if data:
            self.sdo.on_frame(frame.id, data)
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def sdo_upload(self, index: int, subindex: int = 0, on_done=None) -> bool:
        return self.sdo.upload(index, subindex, on_done=on_done)

    def sdo_download(
        self, index: int, subindex: int, value: int, size: int = 4, on_done=None
    ) -> bool:
        return self.sdo.download(index, subindex, value, size, on_done=on_done)

    def shutdown(self) -> None:
        self.sdo.cancel()
