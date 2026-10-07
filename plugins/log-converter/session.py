# -*- coding: utf-8 -*-
"""Shared session — convert job history and recent paths."""

from __future__ import annotations

import os
import time
from dataclasses import asdict, dataclass, field
from typing import Callable, Optional

from _shared import state_store

PLUGIN_ID = "log-converter"
MAX_HISTORY = 80
MAX_RECENT = 16


@dataclass
class JobRecord:
    source: str
    target: str
    fmt: str
    ok: bool = False
    frames: int = 0
    error: str = ""
    started: float = 0.0
    finished: float = 0.0
    batch: bool = False

    @property
    def duration_s(self) -> float:
        if self.started and self.finished:
            return max(0.0, self.finished - self.started)
        return 0.0


class SharedSession:
    def __init__(self, parent=None):
        self._parent = parent
        self._log_fn: Optional[Callable] = None
        self.history: list[JobRecord] = []
        self.recent_sources: list[str] = []
        self.last_fmt: str = "asc"
        self.last_out_dir: str = ""
        self._active_job_id = None
        self._load()

    def set_log_fn(self, fn: Callable):
        self._log_fn = fn

    def log(self, note: str, *, level: str = "SYS"):
        if self._log_fn:
            try:
                self._log_fn(level, "-", b"", note)
            except Exception:
                pass

    def note_source(self, path: str):
        if not path:
            return
        path = os.path.normpath(path)
        self.recent_sources = [
            p for p in self.recent_sources if os.path.normpath(p) != path]
        self.recent_sources.insert(0, path)
        self.recent_sources = self.recent_sources[:MAX_RECENT]
        self._persist()

    def start_dir(self) -> str:
        for p in self.recent_sources:
            d = os.path.dirname(p)
            if d and os.path.isdir(d):
                return d
        if self.last_out_dir and os.path.isdir(self.last_out_dir):
            return self.last_out_dir
        try:
            import sin
            proj = sin.workspace.get_project_dir()
            if proj and os.path.isdir(proj):
                return proj
        except Exception:
            pass
        return os.path.expanduser("~")

    def add_job(self, rec: JobRecord):
        self.history.insert(0, rec)
        self.history = self.history[:MAX_HISTORY]
        if rec.source:
            self.note_source(rec.source)
        if rec.target:
            self.last_out_dir = os.path.dirname(rec.target) or self.last_out_dir
        if rec.fmt:
            self.last_fmt = rec.fmt
        self._persist()

    def clear_history(self):
        self.history.clear()
        self._persist()

    def set_active_job(self, job_id):
        self._active_job_id = job_id

    def active_job(self):
        return self._active_job_id

    def _load(self):
        data = state_store.load_state(PLUGIN_ID, "session.json") or {}
        self.last_fmt = str(data.get("last_fmt") or "asc")
        self.last_out_dir = str(data.get("last_out_dir") or "")
        recent = data.get("recent_sources") or []
        if isinstance(recent, list):
            self.recent_sources = [p for p in recent if isinstance(p, str)][:MAX_RECENT]
        hist = data.get("history") or []
        if isinstance(hist, list):
            for item in hist[:MAX_HISTORY]:
                if not isinstance(item, dict):
                    continue
                try:
                    self.history.append(JobRecord(
                        source=str(item.get("source") or ""),
                        target=str(item.get("target") or ""),
                        fmt=str(item.get("fmt") or ""),
                        ok=bool(item.get("ok")),
                        frames=int(item.get("frames") or 0),
                        error=str(item.get("error") or ""),
                        started=float(item.get("started") or 0),
                        finished=float(item.get("finished") or 0),
                        batch=bool(item.get("batch")),
                    ))
                except Exception:
                    continue

    def _persist(self):
        state_store.save_state(PLUGIN_ID, {
            "last_fmt": self.last_fmt,
            "last_out_dir": self.last_out_dir,
            "recent_sources": list(self.recent_sources),
            "history": [asdict(r) for r in self.history[:MAX_HISTORY]],
        }, "session.json")

    def shutdown(self):
        self._persist()
