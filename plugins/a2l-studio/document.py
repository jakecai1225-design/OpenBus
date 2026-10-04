# -*- coding: utf-8 -*-
"""Shared in-memory A2L document session for A2L Studio."""

from __future__ import annotations

import os
from typing import Callable, List, Optional

from _shared import a2lparse
from _shared.a2lparse import A2lDocument, A2lSymbol


class A2lDocumentSession:
    """File-layer session used by AppShell (alias: A2lStudioDocument)."""

    def __init__(self):
        self.path: str = ""
        self.doc: A2lDocument = a2lparse.empty_document()
        self.dirty: bool = False
        self.compare_doc: Optional[A2lDocument] = None
        self.compare_path: str = ""
        self._listeners: List[Callable[[], None]] = []
        self.focus_kind: Optional[str] = None
        self.focus_name: Optional[str] = None
        self._validated_ok = False
        self._lint_error_count = 0
        self._post_hint_stage = 0

    @property
    def symbols(self) -> List[A2lSymbol]:
        return self.doc.symbols

    def on_changed(self, fn: Callable[[], None]) -> None:
        self._listeners.append(fn)

    def _notify(self) -> None:
        for fn in list(self._listeners):
            try:
                fn()
            except Exception:
                pass

    def mark_dirty(self, dirty: bool = True) -> None:
        self.dirty = dirty
        self.doc.dirty = dirty
        self._notify()

    def set_doc(self, doc: A2lDocument, path: str = "", dirty: bool = False) -> None:
        self.doc = doc
        self.path = path or getattr(doc, "path", "") or ""
        if self.path:
            self.doc.path = self.path
        self.dirty = dirty
        self.doc.dirty = dirty
        self.reset_next_hint()
        self._notify()

    def new_empty(self) -> None:
        self.set_doc(a2lparse.empty_document(), path="", dirty=False)
        self.focus_kind = None
        self.focus_name = None

    new = new_empty

    def load_path(self, path: str) -> bool:
        try:
            doc = a2lparse.parse_a2l_file(path)
        except OSError:
            return False
        self.set_doc(doc, path=path, dirty=False)
        self.focus_kind = None
        self.focus_name = None
        return True

    def load(self, path: str) -> A2lDocument:
        if not self.load_path(path):
            raise OSError("A2L open failed: %s" % path)
        return self.doc

    def save(self, path: Optional[str] = None) -> bool:
        out = path or self.path
        if not out:
            return False
        text = self.doc.source_text
        if self.dirty or not text:
            text = a2lparse.export_slim_a2l(self.doc)
        try:
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        except OSError:
            return False
        self.path = out
        self.doc.path = out
        self.doc.source_text = text
        self.dirty = False
        self.doc.dirty = False
        self._notify()
        return True

    def set_focus(self, kind: Optional[str], name: Optional[str]) -> None:
        self.focus_kind = kind
        self.focus_name = name
        self._notify()

    def mark_validated(self, ok: bool, error_count: int = 0) -> None:
        self._validated_ok = bool(ok)
        self._lint_error_count = int(error_count)
        self._notify()

    def reset_next_hint(self) -> None:
        self._post_hint_stage = 0
        self._validated_ok = False
        self._lint_error_count = 0

    def advance_next_hint(self) -> None:
        self._post_hint_stage = min(3, self._post_hint_stage + 1)

    def next_hint(self) -> tuple:
        if not self.path and not self.doc.symbols:
            return ("Open A2L…", "a2l.open", {})
        if self._post_hint_stage == 0:
            return ("Objects", "a2l.goto", {"page": "objects"})
        if not self._validated_ok:
            return ("Check", "a2l.goto", {"page": "validate"})
        if self.dirty:
            return ("Save", "a2l.save", {})
        return ("Apply → XCP", "a2l.apply_xcp", {})

    def handoff_payload(self) -> dict:
        return {
            "a2l_path": self.path,
            "from_a2l_studio": True,
            "open_in_a2l": False,
        }

    def update_symbol_address(
            self, kind: str, name: str, address: int) -> bool:
        import re
        sym = self.doc.find(kind, name)
        if sym is None:
            return False
        sym.address = int(address) & 0xFFFFFFFF
        if self.doc.source_text and name in self.doc.source_text:
            pat = re.compile(
                r"(/begin\s+%s\s+%s\b[\s\S]*?)(ECU_ADDRESS\s+)(0x[0-9A-Fa-f]+|\d+)"
                % (re.escape(kind), re.escape(name)),
                re.I)
            if pat.search(self.doc.source_text):
                self.doc.source_text = pat.sub(
                    r"\g<1>\g<2>0x%X" % sym.address,
                    self.doc.source_text, count=1)
            else:
                self.doc.source_text = a2lparse.export_slim_a2l(self.doc)
        else:
            self.doc.source_text = a2lparse.export_slim_a2l(self.doc)
        self.mark_dirty(True)
        return True


# Back-compat alias
A2lStudioDocument = A2lDocumentSession
