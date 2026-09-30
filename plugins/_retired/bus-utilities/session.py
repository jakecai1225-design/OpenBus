# -*- coding: utf-8 -*-
"""SharedSession for Bus Utilities (lightweight)."""

from __future__ import annotations

from typing import Callable, Optional

from PyQt6.QtCore import QObject


class SharedSession(QObject):
    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self._log_fn: Optional[Callable] = None
        self._frame_listeners: list[Callable] = []

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

    def shutdown(self) -> None:
        self._frame_listeners.clear()
