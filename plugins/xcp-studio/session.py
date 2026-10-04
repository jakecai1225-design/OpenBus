# -*- coding: utf-8 -*-
"""SharedSession — A2L model + XCP client for XCP Studio."""

from __future__ import annotations

import csv
import os
import time
from typing import Callable, Dict, List, Optional, Tuple

import sin
from PyQt6.QtCore import QObject, QTimer

from core.a2l_model import decode_raw, encode_value, load_a2l, symbol_bytes
from core.xcp_client import XcpClient
from _shared.a2lparse import A2lDocument, A2lSymbol


class SharedSession(QObject):
    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.a2l_path = ""
        self.doc: Optional[A2lDocument] = None
        self.master_id = 0x6B0
        self.slave_id = 0x6B1
        self.live_values: Dict[Tuple[str, str], str] = {}
        self.measure_set: List[Tuple[str, str]] = []  # (kind, name)
        self.recording = False
        self.record_rows: List[list] = []
        self.cal_page = 0
        self.daq_running = False
        self._log_fn: Optional[Callable] = None
        self._listeners: List[Callable] = []
        self._a2l_listeners: List[Callable] = []
        self._post_stage = 0

        def _send(cid, data):
            sin.frames.send(cid, data)

        self.xcp = XcpClient(_send, parent=self, timeout_ms=600)
        self.xcp.set_ids(self.master_id, self.slave_id)
        self.xcp.on_log = self._xcp_log
        self.xcp.on_daq = self._on_daq

        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(200)
        self._poll_timer.timeout.connect(self._poll_tick)
        self._poll_queue: List[Tuple[str, str]] = []
        self._polling = False

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def _xcp_log(self, d, cid, pdu, note):
        self.log(d, cid, pdu, note)

    def on_changed(self, cb: Callable) -> None:
        self._listeners.append(cb)

    def on_a2l_changed(self, cb: Callable) -> None:
        self._a2l_listeners.append(cb)

    def _notify(self) -> None:
        for cb in list(self._listeners):
            try:
                cb()
            except Exception:
                pass

    def _notify_a2l(self) -> None:
        for cb in list(self._a2l_listeners):
            try:
                cb()
            except Exception:
                pass

    def set_ids(self, master_id: int, slave_id: int) -> None:
        self.master_id = int(master_id) & 0x7FF
        self.slave_id = int(slave_id) & 0x7FF
        self.xcp.set_ids(self.master_id, self.slave_id)
        self.log("SYS", "-", b"", "XCP IDs master=0x%X slave=0x%X" % (
            self.master_id, self.slave_id))
        self._notify()

    def load_a2l_path(self, path: str) -> bool:
        try:
            self.doc = load_a2l(path)
        except OSError as e:
            self.log("ERR", "-", b"", "A2L open failed: %s" % e)
            return False
        self.a2l_path = path
        self._post_stage = 0
        self.log("SYS", "-", b"", "Loaded A2L %s (%d symbols)" % (
            path, len(self.doc.symbols)))
        self._notify_a2l()
        self._notify()
        return True

    def symbols(self) -> List[A2lSymbol]:
        return list(self.doc.symbols) if self.doc else []

    def find_symbol(self, kind: str, name: str) -> Optional[A2lSymbol]:
        if not self.doc:
            return None
        return self.doc.find(kind, name)

    def connect_xcp(self, on_done=None) -> bool:
        def done(ok, payload, note):
            self._notify()
            if on_done:
                on_done(ok, payload, note)
        return self.xcp.connect(on_done=done)

    def disconnect_xcp(self, on_done=None) -> bool:
        self.stop_polling()
        def done(ok, payload, note):
            self._notify()
            if on_done:
                on_done(ok, payload, note)
        return self.xcp.disconnect(on_done=done)

    def start_polling(self, keys: List[Tuple[str, str]]) -> None:
        self.measure_set = list(keys)
        self._poll_queue = list(keys)
        self._polling = True
        if not self._poll_timer.isActive():
            self._poll_timer.start()

    def stop_polling(self) -> None:
        self._polling = False
        self._poll_timer.stop()
        self._poll_queue.clear()

    def _poll_tick(self) -> None:
        if not self._polling or self.xcp.busy or not self.xcp.connected:
            return
        if not self._poll_queue:
            self._poll_queue = list(self.measure_set)
        if not self._poll_queue:
            return
        kind, name = self._poll_queue.pop(0)
        sym = self.find_symbol(kind, name)
        if sym is None or not sym.address:
            return
        size = symbol_bytes(sym)

        def done(ok, payload, note):
            if ok and payload is not None:
                raw = bytes(payload)[:size]
                val = decode_raw(raw, sym.datatype)
                disp = ("%g" % val) if sym.datatype.upper().startswith("FLOAT") else (
                    "0x%X" % int(val))
                self.live_values[(kind, name)] = disp
                if self.recording:
                    self.record_rows.append([
                        "%.3f" % time.time(), kind, name, disp])
                self._notify()

        self.xcp.short_upload(
            sym.address, size=size, addr_ext=sym.address_ext, on_done=done)

    def _on_daq(self, pid: int, payload: bytes) -> None:
        # Map first measure entry for DAQ lite display
        if not self.measure_set:
            return
        kind, name = self.measure_set[0]
        sym = self.find_symbol(kind, name)
        if sym is None:
            return
        val = decode_raw(payload, sym.datatype)
        disp = "%g" % val
        self.live_values[(kind, name)] = disp
        if self.recording:
            self.record_rows.append([
                "%.3f" % time.time(), kind, name, disp])
        self._notify()

    def write_characteristic(
        self, name: str, value: float, on_done=None
    ) -> bool:
        sym = self.find_symbol("CHARACTERISTIC", name)
        if sym is None or not sym.address:
            if on_done:
                on_done(False, None, "Symbol not found")
            return False
        data = encode_value(value, sym.datatype or "ULONG")
        return self.xcp.download(
            sym.address, data, addr_ext=sym.address_ext, on_done=on_done)

    def switch_cal_page(self, page: int, on_done=None) -> bool:
        self.cal_page = int(page) & 0xFF
        return self.xcp.set_cal_page(self.cal_page, on_done=on_done)

    def start_daq(self, on_done=None) -> bool:
        def done(ok, payload, note):
            if ok:
                self.daq_running = True
            self._notify()
            if on_done:
                on_done(ok, payload, note)
        return self.xcp.start_daq(on_done=done)

    def stop_daq(self, on_done=None) -> bool:
        def done(ok, payload, note):
            self.daq_running = False
            self._notify()
            if on_done:
                on_done(ok, payload, note)
        return self.xcp.stop_daq(on_done=done)

    def write_map_cell(
        self, name: str, col: int, row: int, value: float, on_done=None
    ) -> bool:
        """Lite MAP write: sequential elements after the characteristic address."""
        sym = self.find_symbol("CHARACTERISTIC", name)
        if sym is None or not sym.address:
            if on_done:
                on_done(False, None, "MAP not found")
            return False
        size = symbol_bytes(sym)
        offset = (int(row) * 8 + int(col)) * size
        data = encode_value(value, sym.datatype or "ULONG")
        return self.xcp.download(
            sym.address + offset, data, addr_ext=sym.address_ext, on_done=on_done)

    def start_record(self) -> None:
        self.recording = True
        self.record_rows = [["t", "kind", "name", "value"]]
        self.log("SYS", "-", b"", "Recording started")

    def stop_record(self) -> None:
        self.recording = False
        self.log("SYS", "-", b"", "Recording stopped (%d rows)" % max(
            0, len(self.record_rows) - 1))

    def export_csv(self, path: str) -> bool:
        try:
            with open(path, "w", encoding="utf-8", newline="") as f:
                w = csv.writer(f)
                for row in self.record_rows:
                    w.writerow(row)
        except OSError as e:
            self.log("ERR", "-", b"", "CSV export failed: %s" % e)
            return False
        self.log("SYS", "-", b"", "CSV exported %s" % path)
        return True

    def export_mdf_stub(self, path: str) -> bool:
        """Minimal MDF4-like text stub (not binary MDF)."""
        try:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write("# OpenBus XCP Studio MDF subset (text)\n")
                f.write("# Not a binary ASAM MDF4 file — CSV-compatible rows\n")
                for row in self.record_rows:
                    f.write(",".join(str(x) for x in row) + "\n")
        except OSError as e:
            self.log("ERR", "-", b"", "MDF stub failed: %s" % e)
            return False
        self.log("SYS", "-", b"", "MDF subset exported %s" % path)
        return True

    def on_frame(self, frame) -> None:
        data = bytes(frame.data) if frame.data else b""
        if data:
            self.xcp.on_frame(frame.id, data)

    def next_hint(self) -> tuple:
        if not self.doc:
            return ("Open A2L…", "xcp.open_a2l", {})
        if not self.xcp.connected:
            return ("Connect", "xcp.connect", {})
        if not self.measure_set:
            return ("Pick signals", "xcp.goto", {"page": "measure"})
        if self._post_stage <= 0:
            return ("Start poll", "xcp.start_poll", {})
        if self._post_stage == 1:
            return ("Calibrate", "xcp.goto", {"page": "calibrate"})
        return ("Export CSV", "xcp.export_csv", {})

    def advance_hint(self) -> None:
        self._post_stage = min(2, self._post_stage + 1)

    def shutdown(self) -> None:
        self.stop_polling()
        self.xcp.cancel()
