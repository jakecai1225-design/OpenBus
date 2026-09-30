# -*- coding: utf-8 -*-
"""SharedSession — channel filter hint + frame fan-out for Protocol Hub."""

from __future__ import annotations

from typing import Callable, Optional

from PyQt6.QtCore import QObject


class SharedSession(QObject):
    """Suite-wide state: channel filter hint and bus frame listeners."""

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.channel_hint = ""  # e.g. "CH0" / "vcan0" — display/filter hint only
        self._log_fn: Optional[Callable] = None
        self._frame_listeners: list[Callable] = []
        self._hint_listeners: list[Callable[[], None]] = []

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_bus_frame(self, cb: Callable) -> None:
        self._frame_listeners.append(cb)

    def on_hint_changed(self, cb: Callable[[], None]) -> None:
        self._hint_listeners.append(cb)

    def set_channel_hint(self, hint: str) -> None:
        self.channel_hint = (hint or "").strip()
        self.log("SYS", "-", b"", "Channel filter hint: %s" % (
            self.channel_hint or "(none)"))
        for cb in list(self._hint_listeners):
            try:
                cb()
            except Exception:
                pass

    def on_frame(self, frame) -> None:
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def shutdown(self) -> None:
        self._frame_listeners.clear()
