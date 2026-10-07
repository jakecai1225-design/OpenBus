# -*- coding: utf-8 -*-
"""Context Attach — Cursor-style Add to AI Chat attachments.

Everything valuable can become an Attachment chip in the AI session.
Large payloads stay lazy; resolve() expands under a byte budget.
"""

from __future__ import annotations

import json
import os
import threading
import uuid
from dataclasses import asdict, dataclass, field
from typing import Any, Callable, Dict, List


@dataclass
class Attachment:
    kind: str
    title: str
    uri: str = ""
    mime: str = "application/json"
    preview: str = ""
    payload: Dict[str, Any] = field(default_factory=dict)
    strategy: str = "inline"  # inline | lazy | summarize
    id: str = ""
    provenance: Dict[str, Any] = field(default_factory=dict)
    ttl: str = "session"

    def __post_init__(self):
        if not self.id:
            self.id = uuid.uuid4().hex[:12]

    def to_dict(self) -> dict:
        return asdict(self)

    @staticmethod
    def from_dict(data: dict) -> "Attachment":
        known = {f.name for f in Attachment.__dataclass_fields__.values()}  # type: ignore
        kwargs = {k: v for k, v in (data or {}).items() if k in known}
        return Attachment(**kwargs)


def eds_attachment(path: str, summary: Dict[str, Any] | None = None,
                   title: str = "") -> Attachment:
    path = os.path.abspath(path) if path else ""
    name = os.path.basename(path) if path else "EDS"
    preview = name
    if summary:
        n = summary.get("object_count")
        if n is not None:
            preview = "%s · %s objects" % (name, n)
    return Attachment(
        kind="eds",
        title=title or name,
        uri=("file:///" + path.replace("\\", "/")) if path else "openbus://eds",
        preview=preview,
        payload={"path": path, "summary": summary or {}},
        strategy="lazy" if path else "inline",
        provenance={"source": "canopen-eds"},
    )


def frames_attachment(frames: List[dict], title: str = "") -> Attachment:
    n = len(frames)
    preview_ids = []
    for fr in frames[:5]:
        cid = fr.get("id")
        if isinstance(cid, int):
            preview_ids.append("0x%X" % cid)
        elif cid is not None:
            preview_ids.append(str(cid))
    preview = "%d frame(s)" % n
    if preview_ids:
        preview += " · " + ", ".join(preview_ids)
    return Attachment(
        kind="trace_frames",
        title=title or ("Trace ×%d" % n),
        uri="openbus://trace/frames",
        preview=preview,
        payload={"frames": frames[:200], "count": n},
        strategy="inline" if n <= 40 else "summarize",
        provenance={"source": "trace"},
    )


def file_attachment(path: str) -> Attachment:
    path = os.path.abspath(path)
    name = os.path.basename(path)
    return Attachment(
        kind="file",
        title=name,
        uri="file:///" + path.replace("\\", "/"),
        preview=path,
        payload={"path": path},
        strategy="lazy",
        provenance={"source": "filesystem"},
    )


def log_rows_attachment(rows: List[dict], title: str = "OUTPUT") -> Attachment:
    return Attachment(
        kind="output_rows",
        title=title,
        uri="openbus://output/rows",
        preview="%d row(s)" % len(rows),
        payload={"rows": rows[:100], "count": len(rows)},
        strategy="inline",
        provenance={"source": "output"},
    )


def resolve(attachments: List[Attachment], budget: int = 12000) -> str:
    """Expand attachments into a text block for the model (under budget)."""
    parts = []
    used = 0
    for att in attachments:
        block = _resolve_one(att, max(512, budget - used))
        if not block:
            continue
        chunk = "### Attachment: %s (%s)\n%s\n" % (att.title, att.kind, block)
        if used + len(chunk) > budget:
            parts.append("### Attachment: %s\n(truncated — budget)\n" % att.title)
            break
        parts.append(chunk)
        used += len(chunk)
    return "\n".join(parts)


def _resolve_one(att: Attachment, limit: int) -> str:
    if att.kind in ("file", "eds"):
        path = (att.payload or {}).get("path") or ""
        summary = (att.payload or {}).get("summary")
        if summary and (not path or not os.path.isfile(path)):
            text = json.dumps(summary, ensure_ascii=False, indent=2)
            return text[:limit] + ("\n...(truncated)" if len(text) > limit else "")
        if not path or not os.path.isfile(path):
            return "(file missing) %s" % att.uri
        try:
            size = os.path.getsize(path)
            if size > limit:
                with open(path, "r", encoding="utf-8", errors="replace") as fh:
                    text = fh.read(limit)
                return text + "\n...(truncated file %d bytes)" % size
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                return fh.read()
        except OSError as exc:
            return "error reading file: %s" % exc

    if att.strategy == "summarize" and att.kind == "trace_frames":
        frames = (att.payload or {}).get("frames") or []
        counts: Dict[str, int] = {}
        for fr in frames:
            cid = fr.get("id")
            key = ("0x%X" % cid) if isinstance(cid, int) else str(cid)
            counts[key] = counts.get(key, 0) + 1
        top = sorted(counts.items(), key=lambda x: -x[1])[:20]
        summary = {
            "count": (att.payload or {}).get("count", len(frames)),
            "top_ids": top,
            "sample": frames[:8],
        }
        text = json.dumps(summary, ensure_ascii=False, indent=2)
    else:
        text = json.dumps(att.payload or {}, ensure_ascii=False, indent=2)
    if len(text) > limit:
        return text[:limit] + "\n...(truncated)"
    return text


class ContextInbox:
    """Process-wide session attachments for the AI chat window (chips)."""

    def __init__(self):
        self._lock = threading.RLock()
        self._items: List[Attachment] = []
        self._listeners: List[Callable[[Attachment], None]] = []

    def push(self, attachment: Attachment | dict) -> Attachment:
        att = (
            attachment if isinstance(attachment, Attachment)
            else Attachment.from_dict(attachment)
        )
        with self._lock:
            self._items.append(att)
            listeners = list(self._listeners)
        for cb in listeners:
            try:
                cb(att)
            except Exception:
                pass
        return att

    def push_many(self, items: List[Attachment | dict]) -> List[Attachment]:
        return [self.push(item) for item in items]

    def items(self) -> List[Attachment]:
        with self._lock:
            return list(self._items)

    def remove(self, att_id: str) -> bool:
        with self._lock:
            before = len(self._items)
            self._items = [a for a in self._items if a.id != att_id]
            return len(self._items) < before

    def clear(self) -> None:
        with self._lock:
            self._items.clear()

    def drain(self) -> List[Attachment]:
        """Take all items (legacy). Prefer items() + keep for chips."""
        with self._lock:
            items = list(self._items)
            self._items.clear()
            return items

    def on_push(self, callback: Callable[[Attachment], None]) -> None:
        with self._lock:
            self._listeners.append(callback)

    def clear_listeners(self) -> None:
        with self._lock:
            self._listeners.clear()


_INBOX = ContextInbox()


def inbox() -> ContextInbox:
    return _INBOX


def get_inbox() -> ContextInbox:
    return _INBOX


def attach(attachment: Attachment | dict) -> Attachment:
    return _INBOX.push(attachment)


def attach_frames(frames: List[dict], title: str = "") -> Attachment:
    return attach(frames_attachment(frames, title=title))


def attach_eds(path: str, summary: Dict[str, Any] | None = None,
               title: str = "") -> Attachment:
    return attach(eds_attachment(path, summary=summary, title=title))


def attach_from_host_params(params: dict) -> List[Attachment]:
    """Handle ai.attach notification params from the C++ host."""
    items = params.get("attachments") or []
    if not items and params.get("kind"):
        items = [params]
    out = []
    for raw in items:
        if not isinstance(raw, dict):
            continue
        kind = raw.get("kind") or "custom"
        if kind == "trace_frames":
            frames = raw.get("frames") or (raw.get("payload") or {}).get("frames") or []
            out.append(attach_frames(frames, title=raw.get("title") or ""))
        elif kind == "eds":
            path = raw.get("path") or (raw.get("payload") or {}).get("path") or ""
            summary = raw.get("summary") or (raw.get("payload") or {}).get("summary")
            out.append(attach_eds(path, summary=summary, title=raw.get("title") or ""))
        else:
            out.append(attach(Attachment.from_dict(raw)))
    return out


def add_attach_action(menu, build_attachment_fn, label: str = "Add to AI Chat"):
    """Qt helper: add a menu action that pushes an attachment and opens AI."""
    from PyQt6.QtGui import QAction

    act = QAction(label, menu)

    def _on():
        att = build_attachment_fn()
        if att is None:
            return
        if isinstance(att, list):
            for item in att:
                attach(item)
        else:
            attach(att)
        try:
            import sin
            sin.commands.execute("aiAgent.open")
        except Exception:
            pass

    act.triggered.connect(_on)
    menu.addAction(act)
    return act
