# -*- coding: utf-8 -*-
"""Shared in-memory EDS/DCF document for EDS Studio."""

from __future__ import annotations

import copy
import os
from typing import Callable, List, Optional, Tuple

from _shared import edsparse

_MAX_HISTORY = 40


class EdsDocument:
    """Primary edsparse.EdsDocument (+ compare peer) with dirty + undo."""

    def __init__(self):
        self.path: str = ""
        self.eds: edsparse.EdsDocument = edsparse.empty_document()
        self.dirty: bool = False
        self.compare_eds: Optional[edsparse.EdsDocument] = None
        self.compare_path: str = ""
        self._listeners: List[Callable[[], None]] = []
        self._baseline: edsparse.EdsDocument = copy.deepcopy(self.eds)
        self._baseline_dirty: bool = False
        self._undo: List[Tuple[edsparse.EdsDocument, bool]] = []
        self._redo: List[Tuple[edsparse.EdsDocument, bool]] = []
        self._suspend_hist: bool = False

    @property
    def entries(self):
        return self.eds.entries

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
        self._baseline = copy.deepcopy(self.eds)
        self._baseline_dirty = self.dirty

    def _push_undo(self, snap: edsparse.EdsDocument, dirty: bool) -> None:
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
        self._redo.append((copy.deepcopy(self.eds), self.dirty))
        snap, dirty = self._undo.pop()
        self._suspend_hist = True
        self.eds = snap
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.eds)
        self._baseline_dirty = self.dirty
        self._notify()
        self._suspend_hist = False
        return True

    def redo(self) -> bool:
        if not self._redo:
            return False
        self._push_undo(copy.deepcopy(self.eds), self.dirty)
        snap, dirty = self._redo.pop()
        self._suspend_hist = True
        self.eds = snap
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.eds)
        self._baseline_dirty = self.dirty
        self._notify()
        self._suspend_hist = False
        return True

    def mark_dirty(self, dirty: bool = True) -> None:
        if dirty and not self._suspend_hist:
            self._push_undo(
                copy.deepcopy(self._baseline), self._baseline_dirty)
            self._redo.clear()
        self.dirty = dirty
        self._baseline = copy.deepcopy(self.eds)
        self._baseline_dirty = self.dirty
        self._notify()

    def set_eds(
            self, eds: edsparse.EdsDocument, path: str = "",
            dirty: bool = False) -> None:
        self.eds = eds
        self.path = path or getattr(eds, "path", "") or ""
        if self.path:
            self.eds.path = self.path
        self.dirty = dirty
        self._clear_history()
        self._notify()

    def new(self) -> None:
        self.set_eds(edsparse.empty_document(), path="", dirty=False)

    def load(self, path: str) -> edsparse.EdsDocument:
        eds = edsparse.parse_eds_file_document(path)
        self.set_eds(eds, path=path, dirty=False)
        return eds

    def save(self, path: Optional[str] = None, as_dcf: Optional[bool] = None) -> str:
        out = path or self.path
        if not out:
            raise ValueError("No path for save")
        use_dcf = as_dcf
        if use_dcf is None:
            use_dcf = out.lower().endswith(".dcf") or self.eds.is_dcf
        self.eds.path = out
        self.eds.is_dcf = bool(use_dcf)
        text = edsparse.export_document(self.eds, as_dcf=use_dcf)
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
        self.path = out
        self.dirty = False
        self._baseline = copy.deepcopy(self.eds)
        self._baseline_dirty = False
        self._notify()
        return out

    def apply_eds(self, eds: edsparse.EdsDocument) -> None:
        if not self._suspend_hist:
            self._push_undo(
                copy.deepcopy(self._baseline), self._baseline_dirty)
            self._redo.clear()
        self.eds = eds
        if self.path:
            self.eds.path = self.path
        self.dirty = True
        self._baseline = copy.deepcopy(self.eds)
        self._baseline_dirty = True
        self._notify()

    def clone_eds(self) -> edsparse.EdsDocument:
        return copy.deepcopy(self.eds)

    def replace_entries(self, entries: list) -> None:
        eds = self.clone_eds()
        eds.entries = list(entries)
        self.apply_eds(eds)

    def upsert_entries(
            self, new_entries: list, merge: bool = True,
            overwrite: bool = True) -> dict:
        """Merge profile/library objects into the open document.

        Returns counts: {"added", "updated", "skipped"}.
        When overwrite=False, existing (index,sub) keys are left unchanged
        (CANeds-style "insert missing only").
        """
        eds = self.clone_eds()
        added = updated = skipped = 0
        if not merge:
            eds.entries = list(new_entries)
            added = len(new_entries)
        else:
            by_key = {(e.index, e.subindex): e for e in eds.entries}
            for e in new_entries:
                key = (e.index, e.subindex)
                if key in by_key:
                    if overwrite:
                        by_key[key] = copy.deepcopy(e)
                        updated += 1
                    else:
                        skipped += 1
                else:
                    by_key[key] = copy.deepcopy(e)
                    added += 1
            eds.entries = sorted(
                by_key.values(), key=lambda x: (x.index, x.subindex))
            self._sync_sub_numbers(eds)
        self.apply_eds(eds)
        return {"added": added, "updated": updated, "skipped": skipped}

    @staticmethod
    def _sync_sub_numbers(eds: edsparse.EdsDocument) -> None:
        """Keep SubNumber on sub-0 records consistent with present subs."""
        by_index: dict = {}
        for e in eds.entries:
            by_index.setdefault(e.index, []).append(e)
        for _idx, group in by_index.items():
            max_sub = max(e.subindex for e in group)
            for e in group:
                if e.subindex == 0 and max_sub > 0:
                    e.extra["SubNumber"] = str(max_sub)

    def display_name(self) -> str:
        if self.path:
            return os.path.basename(self.path)
        return "Untitled.eds"

    def strip_label(self) -> str:
        name = self.path or "(unsaved)"
        return "%s%s" % (name, " *" if self.dirty else "")

    def summary(self) -> dict:
        return {
            "object_count": len(self.eds.entries),
            "indexes": len({e.index for e in self.eds.entries}),
            "is_dcf": bool(self.eds.is_dcf),
            "product": self.eds.device_info.get("ProductName", ""),
        }
