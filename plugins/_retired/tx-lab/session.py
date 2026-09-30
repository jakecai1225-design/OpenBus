# -*- coding: utf-8 -*-
"""SharedSession — DBC path, TX controllers, frame fan-out for TX Lab."""

from __future__ import annotations

import os
from typing import Callable, Optional

from PyQt6.QtCore import QObject

from _shared import dbcparse


class SharedSession(QObject):
    """Suite-wide DBC + Start/Stop-all TX + bus frame distribution."""

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.dbc_path = ""
        self.dbc = None
        self.bus_status = "Bus idle — no TX"

        self._log_fn: Optional[Callable] = None
        self._dbc_listeners: list[Callable[[], None]] = []
        self._status_listeners: list[Callable[[str], None]] = []
        self._frame_listeners: list[Callable] = []
        # name -> (start_fn, stop_fn, is_running_fn)
        self._tx_controllers: dict[str, tuple] = {}

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, source: str, message: str, color: str | None = None) -> None:
        if self._log_fn:
            self._log_fn(source, message, color)

    def on_dbc_changed(self, cb: Callable[[], None]) -> None:
        self._dbc_listeners.append(cb)

    def on_status_changed(self, cb: Callable[[str], None]) -> None:
        self._status_listeners.append(cb)

    def on_bus_frame(self, cb: Callable) -> None:
        self._frame_listeners.append(cb)

    def _notify_dbc(self) -> None:
        for cb in list(self._dbc_listeners):
            try:
                cb()
            except Exception:
                pass

    def _notify_status(self) -> None:
        for cb in list(self._status_listeners):
            try:
                cb(self.bus_status)
            except Exception:
                pass

    def set_bus_status(self, text: str) -> None:
        self.bus_status = text or "Bus idle — no TX"
        self._notify_status()

    def refresh_bus_status(self) -> None:
        running = [
            name for name, (_a, _b, is_run) in self._tx_controllers.items()
            if is_run and is_run()
        ]
        if not running:
            self.set_bus_status("Bus idle — no TX")
        elif len(running) == 1:
            self.set_bus_status("TX running: %s" % running[0])
        else:
            self.set_bus_status("TX running: %s" % ", ".join(running))

    def register_tx_controller(
        self,
        name: str,
        start_fn: Callable[[], bool],
        stop_fn: Callable[[], None],
        is_running_fn: Callable[[], bool],
    ) -> None:
        self._tx_controllers[name] = (start_fn, stop_fn, is_running_fn)

    def start_all_tx(self) -> int:
        started = 0
        for name, (start_fn, _stop, is_run) in list(self._tx_controllers.items()):
            if is_run and is_run():
                continue
            try:
                if start_fn():
                    started += 1
            except Exception as e:
                self.log("ERR", "Start %s failed: %s" % (name, e), "ERR")
        self.refresh_bus_status()
        if started:
            self.log("SYS", "Start all TX (%d source(s))" % started)
        return started

    def stop_all_tx(self) -> None:
        for name, (_start, stop_fn, is_run) in list(self._tx_controllers.items()):
            try:
                if is_run and is_run():
                    stop_fn()
            except Exception as e:
                self.log("ERR", "Stop %s failed: %s" % (name, e), "ERR")
        self.refresh_bus_status()
        self.log("SYS", "Stop all TX")

    def any_tx_running(self) -> bool:
        for _name, (_a, _b, is_run) in self._tx_controllers.items():
            if is_run and is_run():
                return True
        return False

    def load_dbc(self, path: str) -> bool:
        try:
            db = dbcparse.parse_file(path)
        except OSError as e:
            self.log("ERR", "DBC open failed: %s" % e, "ERR")
            return False
        if not db.messages:
            self.log("ERR", "DBC has no messages: %s" % path, "ERR")
            return False
        self.dbc = db
        self.dbc_path = path
        self._notify_dbc()
        self.log(
            "OK",
            "DBC loaded: %s (%d messages)" % (
                os.path.basename(path), len(db.messages)),
        )
        return True

    def clear_dbc(self) -> None:
        self.dbc = None
        self.dbc_path = ""
        self._notify_dbc()
        self.log("SYS", "DBC cleared")

    def on_frame(self, frame) -> None:
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def shutdown(self) -> None:
        self.stop_all_tx()
        self._tx_controllers.clear()
        self._frame_listeners.clear()
