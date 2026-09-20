# -*- coding: utf-8 -*-
"""Shared EtherCAT session: slave list, ESI device, cycle time.

This suite models the network and decodes captures. It does not open a NIC.
"""

from __future__ import annotations

import os
from typing import Callable, Optional

from PyQt6.QtCore import QObject

from core.esi import parse_esi

_ROOT = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.join(_ROOT, "samples", "demo_slave.xml")


class Slave:
    def __init__(self, name="Slave", position=0, vendor_id=0, product_code=0,
                 state="INIT", cable_m=1.0, device=None, esi_path=""):
        self.name = name
        self.position = position
        self.vendor_id = vendor_id
        self.product_code = product_code
        self.state = state
        self.cable_m = cable_m
        self.device = device
        self.esi_path = esi_path

    def to_dict(self) -> dict:
        return {
            "name": self.name,
            "position": self.position,
            "vendor_id": self.vendor_id,
            "product_code": self.product_code,
            "state": self.state,
            "cable_m": self.cable_m,
            "esi_path": self.esi_path,
        }

    @staticmethod
    def from_dict(d: dict) -> "Slave":
        return Slave(
            name=str(d.get("name") or "Slave"),
            position=int(d.get("position") or 0),
            vendor_id=int(d.get("vendor_id") or 0),
            product_code=int(d.get("product_code") or 0),
            state=str(d.get("state") or "INIT"),
            cable_m=float(d.get("cable_m") or 1.0),
            esi_path=str(d.get("esi_path") or ""),
        )


class SharedSession(QObject):
    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.slaves: list = []
        self.selected = 0
        self.cycle_ns = 1_000_000
        self.frame_bytes = 64
        self._log_fn = None
        self._changed: list[Callable] = []

    def set_log_fn(self, fn) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None) -> None:
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_changed(self, cb: Callable) -> None:
        self._changed.append(cb)

    def notify(self) -> None:
        for cb in list(self._changed):
            try:
                cb()
            except Exception:
                pass

    def current(self):
        if not self.slaves:
            return None
        self.selected = max(0, min(self.selected, len(self.slaves) - 1))
        return self.slaves[self.selected]

    def load_sample(self) -> bool:
        if not os.path.isfile(SAMPLE):
            return False
        try:
            device = parse_esi(SAMPLE)
        except (OSError, ValueError) as exc:
            self.log("ERR", "-", b"", "Sample ESI failed: %s" % exc)
            return False
        slave = Slave(
            name=device.name,
            position=0,
            vendor_id=device.vendor_id,
            product_code=device.product_code,
            state="INIT",
            cable_m=1.0,
            device=device,
            esi_path=SAMPLE,
        )
        self.slaves = [slave]
        self.selected = 0
        self.notify()
        self.log("SYS", "-", b"", "Demo slave: %s" % device.name)
        return True

    def load_esi(self, path: str) -> bool:
        try:
            device = parse_esi(path)
        except (OSError, ValueError) as exc:
            self.log("ERR", "-", b"", "ESI failed: %s" % exc)
            return False
        slave = self.current()
        if slave is None:
            slave = Slave(position=0)
            self.slaves.append(slave)
            self.selected = 0
        slave.device = device
        slave.esi_path = path
        slave.name = device.name
        slave.vendor_id = device.vendor_id
        slave.product_code = device.product_code
        self.notify()
        self.log("SYS", "-", b"", "ESI %s" % device.name)
        return True

    def to_state(self) -> dict:
        return {
            "slaves": [s.to_dict() for s in self.slaves],
            "selected": self.selected,
            "cycle_ns": self.cycle_ns,
            "frame_bytes": self.frame_bytes,
        }

    def apply_state(self, saved: dict) -> None:
        if not saved:
            return
        rows = saved.get("slaves") or []
        if rows:
            self.slaves = [Slave.from_dict(r) for r in rows]
            self.selected = int(saved.get("selected") or 0)
        self.cycle_ns = int(saved.get("cycle_ns") or self.cycle_ns)
        self.frame_bytes = int(saved.get("frame_bytes") or self.frame_bytes)
        for slave in self.slaves:
            path = slave.esi_path
            if path and os.path.isfile(path):
                try:
                    slave.device = parse_esi(path)
                except (OSError, ValueError):
                    slave.device = None
        self.notify()
