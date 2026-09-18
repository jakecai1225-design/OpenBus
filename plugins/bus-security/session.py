# -*- coding: utf-8 -*-
"""SharedSession — rate-limit defaults, Stop All, frame fan-out for Bus Security."""

from __future__ import annotations

from typing import Callable, Optional

from PyQt6.QtCore import QObject


class SharedSession(QObject):
    """Suite-wide defaults and emergency stop for TX pages (Fuzzer / Stress).

    IDS and E2E register as frame listeners (read-only). Pages must register
    a stop handler so Stop All can abort any active injection.
    """

    def __init__(self, parent: Optional[QObject] = None):
        super().__init__(parent)
        self.default_interval_ms = 50
        self.default_send_limit = 500
        self.default_load_pct = 50.0
        self.default_gap_ms = 0

        self._log_fn: Optional[Callable] = None
        self._stop_handlers: list[Callable[[], None]] = []
        self._frame_listeners: list[Callable] = []
        self._defaults_listeners: list[Callable[[], None]] = []

    def set_log_fn(self, fn: Callable) -> None:
        self._log_fn = fn

    def log(self, source: str, message: str, color: str | None = None) -> None:
        if self._log_fn:
            self._log_fn(source, message, color)

    def on_defaults_changed(self, cb: Callable[[], None]) -> None:
        self._defaults_listeners.append(cb)

    def apply_defaults(
        self,
        interval_ms: Optional[int] = None,
        send_limit: Optional[int] = None,
        load_pct: Optional[float] = None,
        gap_ms: Optional[int] = None,
    ) -> None:
        if interval_ms is not None:
            self.default_interval_ms = max(1, int(interval_ms))
        if send_limit is not None:
            self.default_send_limit = max(0, int(send_limit))
        if load_pct is not None:
            self.default_load_pct = float(load_pct)
        if gap_ms is not None:
            self.default_gap_ms = max(0, int(gap_ms))
        for cb in list(self._defaults_listeners):
            try:
                cb()
            except Exception:
                pass
        self.log(
            "SYS",
            "Defaults: interval %d ms · send limit %s · load %.0f%% · gap %d ms"
            % (
                self.default_interval_ms,
                "unlimited" if self.default_send_limit == 0
                else str(self.default_send_limit),
                self.default_load_pct,
                self.default_gap_ms,
            ),
        )

    def register_stop_handler(self, cb: Callable[[], None]) -> None:
        if cb not in self._stop_handlers:
            self._stop_handlers.append(cb)

    def stop_all(self) -> None:
        """Emergency stop: abort every registered TX / long-running activity."""
        for cb in list(self._stop_handlers):
            try:
                cb()
            except Exception:
                pass
        self.log("WARN", "Stop All — all active TX / monitors asked to halt", "WARN")

    def on_bus_frame(self, cb: Callable) -> None:
        self._frame_listeners.append(cb)

    def on_frame(self, frame) -> None:
        for cb in list(self._frame_listeners):
            try:
                cb(frame)
            except Exception:
                pass

    def shutdown(self) -> None:
        self.stop_all()
        self._stop_handlers.clear()
        self._frame_listeners.clear()
