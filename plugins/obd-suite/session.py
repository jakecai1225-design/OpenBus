# -*- coding: utf-8 -*-
"""SharedSession for OBD Suite — IDs, focus, Context Next."""

from __future__ import annotations

from typing import Callable, Optional

from PyQt6.QtCore import QObject


class SharedSession(QObject):
    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.tx_id = 0x7DF
        self.rx_id = 0x7E8
        self._ids_applied = False
        self._scan_touched = False
        self._readiness_touched = False
        self._post_hint_stage = 0
        self.focus_mode = 0
        self.focus_pid = 0
        self._log_fn: Optional[Callable] = None
        self._frame_listeners: list = []
        self._focus_listeners: list = []
        self._id_listeners: list = []

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_bus_frame(self, cb: Callable) -> None:
        self._frame_listeners.append(cb)

    def on_frame(self, frame) -> None:
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def on_focus(self, cb: Callable) -> None:
        self._focus_listeners.append(cb)

    def on_ids_changed(self, cb: Callable) -> None:
        self._id_listeners.append(cb)

    def apply_ids(self, tx_id: int, rx_id: int) -> None:
        self.tx_id = int(tx_id)
        self.rx_id = int(rx_id)
        self._ids_applied = True
        for cb in list(self._id_listeners):
            try:
                cb(self.tx_id, self.rx_id)
            except Exception:
                pass

    def set_focus(self, mode: int = 0, pid: int = 0, **_kwargs) -> None:
        self.focus_mode = int(mode or 0)
        self.focus_pid = int(pid or 0) & 0xFF
        for cb in list(self._focus_listeners):
            try:
                cb(self.focus_mode, self.focus_pid)
            except Exception:
                pass

    def focus_notify(self) -> None:
        """Re-emit current focus so newly shown pages can select."""
        self.set_focus(mode=self.focus_mode, pid=self.focus_pid)

    def note_scan(self) -> None:
        self._scan_touched = True

    def note_readiness(self) -> None:
        self._readiness_touched = True

    def advance_next_hint(self) -> None:
        self._post_hint_stage = min(4, int(self._post_hint_stage) + 1)

    def next_hint(self) -> tuple:
        """Return (label, action_id, kwargs) for Context Next."""
        if not self._ids_applied:
            return ("Set request IDs", "obd.goto", {"page": "setup"})
        if not self._scan_touched:
            return ("Open Scanner", "obd.goto", {"page": "scanner"})
        if not self._readiness_touched and self._post_hint_stage < 2:
            return ("Check Readiness", "obd.goto", {"page": "readiness"})
        if self._post_hint_stage < 3:
            return ("Discover PIDs", "obd.discover", {})
        return ("Export live data", "obd.export", {})

    def shutdown(self) -> None:
        self._frame_listeners.clear()
        self._focus_listeners.clear()
        self._id_listeners.clear()
