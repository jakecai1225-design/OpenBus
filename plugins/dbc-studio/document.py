# -*- coding: utf-8 -*-
"""Shared in-memory DBC document for DBC Studio."""

from __future__ import annotations

import copy
import os
from typing import Callable, List, Optional

from _shared import dbcparse


class DbcDocument:
    """One primary DbcFile (+ optional compare peer) with dirty tracking."""

    def __init__(self):
        self.path: str = ""
        self.db: dbcparse.DbcFile = self._empty_db()
        self.dirty: bool = False
        self.compare_db: Optional[dbcparse.DbcFile] = None
        self.compare_path: str = ""
        self._listeners: List[Callable[[], None]] = []

    @staticmethod
    def _empty_db() -> dbcparse.DbcFile:
        db = dbcparse.DbcFile()
        db.version = ""
        db.nodes = ["Vector__XXX"]
        return db

    def on_changed(self, fn: Callable[[], None]) -> None:
        self._listeners.append(fn)

    def _notify(self) -> None:
        for fn in list(self._listeners):
            try:
                fn()
            except Exception:
                pass

    def mark_dirty(self, dirty: bool = True) -> None:
        if self.dirty == dirty:
            self._notify()
            return
        self.dirty = dirty
        self._notify()

    def set_db(self, db: dbcparse.DbcFile, path: str = "", dirty: bool = False) -> None:
        self.db = db
        self.path = path or getattr(db, "path", "") or ""
        if self.path:
            self.db.path = self.path
        self.dirty = dirty
        self._notify()

    def new(self) -> None:
        self.set_db(self._empty_db(), path="", dirty=False)

    def load(self, path: str) -> dbcparse.DbcFile:
        db = dbcparse.parse_file(path)
        self.set_db(db, path=path, dirty=False)
        return db

    def save(self, path: Optional[str] = None) -> str:
        out = path or self.path
        if not out:
            raise ValueError("No path for save")
        text = dbcparse.serialize(self.db)
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
        self.path = out
        self.db.path = out
        self.dirty = False
        self._notify()
        return out

    def clone_db(self) -> dbcparse.DbcFile:
        return copy.deepcopy(self.db)

    def display_name(self) -> str:
        if self.path:
            return os.path.basename(self.path)
        return "Untitled.dbc"

    def strip_label(self) -> str:
        name = self.path or "(unsaved)"
        return "%s%s" % (name, " *" if self.dirty else "")
