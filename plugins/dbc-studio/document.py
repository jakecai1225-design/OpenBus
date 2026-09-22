# -*- coding: utf-8 -*-
"""Shared in-memory DBC document for DBC Studio."""

from __future__ import annotations

import copy
import os
from typing import Callable, List, Optional, Tuple

from _shared import dbcparse

_MAX_HISTORY = 40


class DbcDocument:
    """One primary DbcFile (+ optional compare peer) with dirty + undo tracking."""

    def __init__(self):
        self.path: str = ""
        self.db: dbcparse.DbcFile = self._empty_db()
        self.dirty: bool = False
        self.compare_db: Optional[dbcparse.DbcFile] = None
        self.compare_path: str = ""
        self._listeners: List[Callable[[], None]] = []
        self._baseline: dbcparse.DbcFile = copy.deepcopy(self.db)
        self._baseline_dirty: bool = False
        self._undo: List[Tuple[dbcparse.DbcFile, bool]] = []
        self._redo: List[Tuple[dbcparse.DbcFile, bool]] = []
        self._suspend_hist: bool = False

    @staticmethod
    def _empty_db() -> dbcparse.DbcFile:
        db = dbcparse.DbcFile()
        db.version = ""
        db.nodes = ["Vector__XXX"]
        db.ensure_default_attr_defs()
        return db

    def on_changed(self, fn: Callable[[], None]) -> None:
        self._listeners.append(fn)

    def _notify(self) -> None:
        for fn in list(self._listeners):
            try:
                fn()
            except Exception:
                pass

    def _clear_history(self) -> None:
        self._undo.clear()
        self._redo.clear()
        self._baseline = copy.deepcopy(self.db)
        self._baseline_dirty = self.dirty

    def _push_undo(self, snap: dbcparse.DbcFile, dirty: bool) -> None:
        self._undo.append((snap, dirty))
        if len(self._undo) > _MAX_HISTORY:
            self._undo.pop(0)

    def can_undo(self) -> bool:
        return bool(self._undo)

    def can_redo(self) -> bool:
        return bool(self._redo)

    def undo(self) -> bool:
        if not self._undo:
            return False
        self._redo.append((copy.deepcopy(self.db), self.dirty))
        snap, dirty = self._undo.pop()
        self._suspend_hist = True
        self.db = snap
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.db)
        self._baseline_dirty = self.dirty
        self._notify()
        self._suspend_hist = False
        return True

    def redo(self) -> bool:
        if not self._redo:
            return False
        self._push_undo(copy.deepcopy(self.db), self.dirty)
        snap, dirty = self._redo.pop()
        self._suspend_hist = True
        self.db = snap
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.db)
        self._baseline_dirty = self.dirty
        self._notify()
        self._suspend_hist = False
        return True

    def mark_dirty(self, dirty: bool = True) -> None:
        """Mark dirty after a mutation. Pushes undo from pre-edit baseline."""
        if dirty and not self._suspend_hist:
            self._push_undo(
                copy.deepcopy(self._baseline), self._baseline_dirty)
            self._redo.clear()
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.db)
        self._baseline_dirty = self.dirty
        self._notify()

    def set_db(
            self, db: dbcparse.DbcFile, path: str = "",
            dirty: bool = False) -> None:
        self.db = db
        self.path = path or getattr(db, "path", "") or ""
        if self.path:
            self.db.path = self.path
        self.dirty = dirty
        self._clear_history()
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
        self._baseline = copy.deepcopy(self.db)
        self._baseline_dirty = False
        self._notify()
        return out

    def apply_db(self, db: dbcparse.DbcFile) -> None:
        """Replace message/node content, keep path, record undo."""
        if not self._suspend_hist:
            self._push_undo(
                copy.deepcopy(self._baseline), self._baseline_dirty)
            self._redo.clear()
        self.db = db
        if self.path:
            self.db.path = self.path
        self.dirty = True
        self._baseline = copy.deepcopy(self.db)
        self._baseline_dirty = True
        self._notify()

    def clone_db(self) -> dbcparse.DbcFile:
        return copy.deepcopy(self.db)

    def display_name(self) -> str:
        if self.path:
            return os.path.basename(self.path)
        return "Untitled.dbc"

    def strip_label(self) -> str:
        name = self.path or "(unsaved)"
        return "%s%s" % (name, " *" if self.dirty else "")
