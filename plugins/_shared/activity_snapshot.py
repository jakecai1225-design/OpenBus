# -*- coding: utf-8 -*-
"""Activity Snapshot v0 — short 'where am I' block for the system prompt.

Updated off the hot path; never blocks capture. Debounced writes.
Suites and the AI Agent both write here.
"""

from __future__ import annotations

import json
import os
import threading
import time
from typing import Any, Dict

_DEBOUNCE_S = 0.3
_PROMPT_BUDGET = 800

_KNOWN = (
    "project_path", "active_plugin", "active_page",
    "selection_summary", "measure_on",
)

_lock = threading.RLock()
_state: Dict[str, Any] = {
    "project_path": "",
    "active_plugin": "",
    "active_page": "",
    "selection_summary": "",
    "measure_on": False,
}
_pending: Dict[str, Any] = {}
_timer: threading.Timer | None = None
_updated_at = 0.0


def _flush() -> None:
    global _timer, _updated_at
    with _lock:
        _timer = None
        if _pending:
            _state.update(_pending)
            _pending.clear()
            _updated_at = time.time()


def update(**fields: Any) -> None:
    """Merge fields into the snapshot (debounced)."""
    global _timer
    clean = {k: v for k, v in fields.items() if k in _KNOWN}
    if not clean:
        return
    with _lock:
        _pending.update(clean)
        if _timer is not None:
            try:
                _timer.cancel()
            except Exception:
                pass
        _timer = threading.Timer(_DEBOUNCE_S, _flush)
        _timer.daemon = True
        _timer.start()


def get() -> Dict[str, Any]:
    with _lock:
        if _pending:
            out = dict(_state)
            out.update(_pending)
            return out
        return dict(_state)


def touch_from_env() -> None:
    """Best-effort fill from environment / cwd."""
    project = (
        os.environ.get("OPENBUS_PROJECT")
        or os.environ.get("OPENBUS_WORKSPACE")
        or ""
    )
    if not project:
        try:
            project = os.getcwd()
        except OSError:
            project = ""
    fields = {}
    if project and not get().get("project_path"):
        fields["project_path"] = project
    if fields:
        update(**fields)


def format_for_prompt(max_chars: int = _PROMPT_BUDGET) -> str:
    """Return a short block for the system prompt, or empty string."""
    try:
        touch_from_env()
        snap = get()
        lines = ["## Activity Snapshot"]
        if snap.get("project_path"):
            lines.append("project: %s" % snap["project_path"])
        if snap.get("active_plugin"):
            lines.append("plugin: %s" % snap["active_plugin"])
        if snap.get("active_page"):
            lines.append("page: %s" % snap["active_page"])
        if snap.get("selection_summary"):
            lines.append("selection: %s" % snap["selection_summary"])
        lines.append("measure: %s" % ("on" if snap.get("measure_on") else "off"))
        text = "\n".join(lines)
        if len(text) > max_chars:
            return text[: max_chars - 20] + "\n...(truncated)"
        return text
    except Exception:
        return ""


def as_json(max_chars: int = _PROMPT_BUDGET) -> str:
    try:
        text = json.dumps(get(), ensure_ascii=False)
        return text[:max_chars]
    except Exception:
        return "{}"
