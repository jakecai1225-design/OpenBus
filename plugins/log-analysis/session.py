# -*- coding: utf-8 -*-
"""SharedSession — working folder, last paths, bus frame fan-out for Log Analysis."""

from __future__ import annotations

import os
from typing import Callable, Optional

from PyQt6.QtCore import QObject


class SharedSession(QObject):
    """Suite-wide paths + live-frame dispatch for Compare / Trigger / Quality / …"""

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.working_folder = ""
        self.last_log_path = ""
        self.last_dbc_path = ""
        self.last_export_path = ""
        self._path_listeners: list[Callable[[], None]] = []
        self._frame_listeners: list[Callable] = []
        self._log_fn: Optional[Callable] = None
        self._running = True

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, direction, can_id, pdu, note, color=None):
        if self._log_fn:
            self._log_fn(direction, can_id, pdu, note, color)

    def on_paths_changed(self, cb: Callable[[], None]) -> None:
        self._path_listeners.append(cb)

    def on_bus_frame(self, cb: Callable) -> None:
        self._frame_listeners.append(cb)

    def _notify_paths(self) -> None:
        for cb in list(self._path_listeners):
            try:
                cb()
            except Exception:
                pass

    def set_working_folder(self, path: str) -> None:
        path = (path or "").strip()
        if path and not os.path.isdir(path):
            self.log("ERR", "-", b"", "Not a folder: %s" % path)
            return
        self.working_folder = path
        self._notify_paths()
        if path:
            self.log("RX", "-", b"", "Working folder: %s" % path)
        else:
            self.log("RX", "-", b"", "Working folder cleared")

    def note_log_path(self, path: str) -> None:
        if not path:
            return
        self.last_log_path = path
        folder = os.path.dirname(path)
        if folder and os.path.isdir(folder) and not self.working_folder:
            self.working_folder = folder
        self._notify_paths()

    def note_dbc_path(self, path: str) -> None:
        if not path:
            return
        self.last_dbc_path = path
        self._notify_paths()

    def note_export_path(self, path: str) -> None:
        if not path:
            return
        self.last_export_path = path
        self._notify_paths()

    def start_dir(self, preferred: str = "") -> str:
        """Best starting directory for file dialogs."""
        for candidate in (
            preferred,
            self.working_folder,
            os.path.dirname(self.last_log_path) if self.last_log_path else "",
            os.path.dirname(self.last_export_path) if self.last_export_path else "",
        ):
            if candidate and os.path.isdir(candidate):
                return candidate
            if candidate and os.path.isfile(candidate):
                d = os.path.dirname(candidate)
                if d and os.path.isdir(d):
                    return d
        return ""

    def on_frame(self, frame) -> None:
        if not self._running:
            return
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def shutdown(self) -> None:
        self._running = False
        self._frame_listeners.clear()
