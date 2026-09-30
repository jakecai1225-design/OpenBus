# -*- coding: utf-8 -*-
"""SharedSession for J1939 Suite — focus, DBC path, Context Next."""

from __future__ import annotations

from typing import Callable, Optional

from PyQt6.QtCore import QObject


class SharedSession(QObject):
    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.dbc_path = ""
        self.focus_pgn = 0
        self.focus_sa = -1
        self._saw_frame = False
        self._saw_dm = False
        self._saw_tp = False
        self._saw_claim = False
        self._post_hint_stage = 0
        self._log_fn: Optional[Callable] = None
        self._frame_listeners: list[Callable] = []
        self._focus_listeners: list[Callable] = []

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_bus_frame(self, cb: Callable) -> None:
        self._frame_listeners.append(cb)

    def on_frame(self, frame) -> None:
        self._saw_frame = True
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def on_focus(self, cb: Callable) -> None:
        self._focus_listeners.append(cb)

    def set_focus(self, pgn: int = 0, sa: int = -1, **_kwargs) -> None:
        self.focus_pgn = int(pgn or 0)
        self.focus_sa = int(sa) if sa is not None else -1
        for cb in list(self._focus_listeners):
            try:
                cb(self.focus_pgn, self.focus_sa)
            except Exception:
                pass

    def note_dbc(self, path: str) -> None:
        self.dbc_path = path or ""

    def note_dm(self) -> None:
        self._saw_dm = True

    def note_tp(self) -> None:
        self._saw_tp = True

    def note_claim(self) -> None:
        self._saw_claim = True

    def advance_next_hint(self) -> None:
        self._post_hint_stage = min(5, int(self._post_hint_stage) + 1)

    def next_hint(self) -> tuple:
        """Return (label, action_id, kwargs) for Context Next."""
        if not self.dbc_path and self._post_hint_stage < 1:
            return ("Load J1939 DBC", "j1939.load_dbc", {})
        if not self._saw_frame:
            return ("Open Live", "j1939.goto", {"page": "analyzer"})
        if self._saw_dm and self._post_hint_stage < 2:
            return ("View Diagnostics", "j1939.goto", {"page": "diagnostics"})
        if self._saw_tp and self._post_hint_stage < 3:
            return ("View Transport", "j1939.goto", {"page": "transport"})
        if self._saw_claim and self._post_hint_stage < 4:
            return ("View Network", "j1939.goto", {"page": "network"})
        return ("Open Live", "j1939.goto", {"page": "analyzer"})

    def shutdown(self) -> None:
        self._frame_listeners.clear()
        self._focus_listeners.clear()
