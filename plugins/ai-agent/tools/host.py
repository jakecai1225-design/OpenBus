# -*- coding: utf-8 -*-
"""SinHost — adapter over sin.* SDK (or no-op stubs when offline)."""

from __future__ import annotations

from typing import Any, List, Optional


class SinHost:
    """Methods match FakeHost used in unit tests."""

    def __init__(self):
        self._sin = None
        try:
            import sin as _sin  # type: ignore
            self._sin = _sin
        except Exception:
            self._sin = None

    def log(self, text: str) -> None:
        try:
            if self._sin is not None:
                self._sin.output.append(str(text))
        except Exception:
            pass

    def get_recent(self, count: int = 100) -> list:
        if self._sin is None:
            return []
        try:
            return list(self._sin.frames.get_recent(int(count)) or [])
        except Exception:
            return []

    def get_selected(self) -> list:
        if self._sin is None:
            return []
        try:
            return list(self._sin.frames.get_selected() or [])
        except Exception:
            return []

    def send_frame(self, can_id, data, extended: bool = False, fd: bool = False) -> None:
        if self._sin is None:
            raise RuntimeError("sin SDK unavailable — cannot send frame")
        self._sin.frames.send(int(can_id), data, extended=bool(extended), fd=bool(fd))

    def decode(self, can_id, data) -> dict:
        if self._sin is None:
            return {}
        try:
            return dict(self._sin.signals.decode(int(can_id), data) or {})
        except Exception:
            return {}

    def project_dir(self) -> str:
        if self._sin is None:
            return ""
        try:
            return str(self._sin.workspace.get_project_dir() or "")
        except Exception:
            return ""

    def dbc_files(self) -> List[str]:
        if self._sin is None:
            return []
        try:
            files = self._sin.workspace.get_dbc_files()
            return list(files or [])
        except Exception:
            return []

    def list_messages(self) -> List[dict]:
        """Best-effort DBC message list (empty if no dbc RPC)."""
        # Prefer signals/dbc if available; otherwise empty
        try:
            import sin  # noqa: F401
            # No stable list_messages RPC yet — return empty; FakeHost supplies in tests
            return []
        except Exception:
            return []

    def bus_status(self) -> dict:
        """Placeholder device/bus status until host RPC is complete."""
        return {
            "connected": None,
            "measure_on": None,
            "note": "bus_get_status device fields are placeholders (Phase 0 gap)",
        }

    # Aliases used by some tests / older call sites
    def get_recent_frames(self, count: int = 100):
        return self.get_recent(count)

    def get_selected_frames(self):
        return self.get_selected()

    def get_frame_stats(self, count: int = 500):
        from tools.bus_tools import summarize_frames
        return summarize_frames(self.get_recent(count), self, top_n=20)

    def decode_frame(self, can_id, data):
        return self.decode(can_id, data)

    def list_dbc_messages(self):
        return self.list_messages()

    def workspace_paths(self) -> dict:
        return {
            "project_dir": self.project_dir(),
            "dbc_files": self.dbc_files(),
        }
