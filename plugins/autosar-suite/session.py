# -*- coding: utf-8 -*-
"""Shared AUTOSAR session: I-PDUs, NM base, E2E and SecOC settings."""

from __future__ import annotations

import os
from typing import Callable, Optional

from PyQt6.QtCore import QObject

from core.arxml_min import parse_arxml
from core.ipdu import Ipdu

_ROOT = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.join(_ROOT, "samples", "body_can.xml")


class SharedSession(QObject):
    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.ipdus: list = []
        self.active = 0
        self.nm_base = 0x400
        self.e2e_can_id = 0x200
        self.e2e_data_id = 0x1234
        self.e2e_mode = "BOTH"
        self.secoc_can_id = 0x300
        self.secoc_fv_bits = 8
        self.secoc_mac_bits = 16
        self.secoc_key = b"key"
        self.arxml_path = ""
        self._log_fn = None
        self._bus: list[Callable] = []
        self._changed: list[Callable] = []

    def set_log_fn(self, fn) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None) -> None:
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_bus(self, cb: Callable) -> None:
        self._bus.append(cb)

    def on_changed(self, cb: Callable) -> None:
        self._changed.append(cb)

    def notify(self) -> None:
        for cb in list(self._changed):
            try:
                cb()
            except Exception:
                pass

    def active_pdu(self):
        if not self.ipdus:
            return None
        self.active = max(0, min(self.active, len(self.ipdus) - 1))
        return self.ipdus[self.active]

    def replace_ipdus(self, pdus: list, note: str = "") -> None:
        self.ipdus = list(pdus)
        self.active = 0
        self.notify()
        self.log("SYS", "-", b"", note or ("Loaded %d I-PDU(s)" % len(self.ipdus)))

    def load_sample(self) -> bool:
        if not os.path.isfile(SAMPLE):
            return False
        try:
            pdus = parse_arxml(SAMPLE)
        except (OSError, ValueError) as exc:
            self.log("ERR", "-", b"", "Sample ARXML failed: %s" % exc)
            return False
        self.replace_ipdus(pdus, "Demo system: BodyStatus 0x100")
        return True

    def load_arxml(self, path: str) -> bool:
        try:
            pdus = parse_arxml(path)
        except (OSError, ValueError) as exc:
            self.log("ERR", "-", b"", "ARXML failed: %s" % exc)
            return False
        if not pdus:
            self.log("ERR", "-", b"", "No I-PDU found in %s" % path)
            return False
        self.replace_ipdus(pdus, "ARXML %s (%d PDU)" % (path, len(pdus)))
        return True

    def on_frame(self, frame) -> None:
        try:
            cid = int(frame.id)
            data = bytes(frame.data or b"")
        except Exception:
            return
        for cb in list(self._bus):
            try:
                cb(cid, data)
            except Exception:
                pass

    def send(self, can_id: int, data: bytes, note: str = "") -> None:
        payload = bytes(data)
        try:
            import sin
            sin.frames.send(int(can_id), payload)
        except Exception as exc:
            self.log("ERR", can_id, payload, str(exc))
            return
        self.log("TX", can_id, payload, note)

    def to_state(self) -> dict:
        return {
            "ipdus": [p.to_dict() for p in self.ipdus],
            "active": self.active,
            "nm_base": self.nm_base,
            "e2e_can_id": self.e2e_can_id,
            "e2e_data_id": self.e2e_data_id,
            "e2e_mode": self.e2e_mode,
            "secoc_can_id": self.secoc_can_id,
            "secoc_fv_bits": self.secoc_fv_bits,
            "secoc_mac_bits": self.secoc_mac_bits,
            "secoc_key": self.secoc_key.hex(),
            "arxml_path": self.arxml_path,
        }

    def apply_state(self, saved: dict) -> None:
        if not saved:
            return
        rows = saved.get("ipdus") or []
        if rows:
            self.ipdus = [Ipdu.from_dict(r) for r in rows]
            self.active = int(saved.get("active") or 0)
        self.nm_base = int(saved.get("nm_base") or self.nm_base)
        self.e2e_can_id = int(saved.get("e2e_can_id") or self.e2e_can_id)
        self.e2e_data_id = int(saved.get("e2e_data_id") or self.e2e_data_id)
        self.e2e_mode = str(saved.get("e2e_mode") or self.e2e_mode)
        self.secoc_can_id = int(saved.get("secoc_can_id") or self.secoc_can_id)
        self.secoc_fv_bits = int(saved.get("secoc_fv_bits") or self.secoc_fv_bits)
        self.secoc_mac_bits = int(saved.get("secoc_mac_bits") or self.secoc_mac_bits)
        self.arxml_path = str(saved.get("arxml_path") or "")
        key = saved.get("secoc_key")
        if isinstance(key, str) and key:
            try:
                self.secoc_key = bytes.fromhex(key)
            except ValueError:
                pass
        self.notify()
