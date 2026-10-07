# -*- coding: utf-8 -*-
"""Process-wide capability registry for AI and cross-plugin tools.

Suites register on activate and unregister on deactivate.
AI and other callers only use list() / invoke() — never import suite privates.
"""

from __future__ import annotations

import threading
import time
import traceback
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional


Permission = str  # read | write | diag_write | flash


@dataclass
class Capability:
    id: str
    provider: str
    title: str
    description: str
    parameters: Dict[str, Any]
    permission: Permission = "read"
    handler: Optional[Callable[[dict], dict]] = None
    tags: List[str] = field(default_factory=list)
    version: str = "1.0.0"
    stability: str = "experimental"


class CapabilityBus:
    def __init__(self, result_limit: int = 12000):
        self._lock = threading.RLock()
        self._caps: Dict[str, Capability] = {}
        self._by_provider: Dict[str, List[str]] = {}
        self.result_limit = result_limit

    def register(self, plugin_id: str, caps: List[Capability]) -> None:
        with self._lock:
            self.unregister(plugin_id)
            ids = []
            for cap in caps:
                cap.provider = plugin_id
                self._caps[cap.id] = cap
                ids.append(cap.id)
            self._by_provider[plugin_id] = ids

    def unregister(self, plugin_id: str) -> None:
        with self._lock:
            for cid in self._by_provider.pop(plugin_id, []):
                self._caps.pop(cid, None)

    def list(self, provider: Optional[str] = None,
             permission: Optional[str] = None) -> List[dict]:
        with self._lock:
            out = []
            for cap in self._caps.values():
                if provider and cap.provider != provider:
                    continue
                if permission and cap.permission != permission:
                    continue
                out.append({
                    "id": cap.id,
                    "provider": cap.provider,
                    "title": cap.title,
                    "description": cap.description,
                    "parameters": cap.parameters,
                    "permission": cap.permission,
                    "tags": list(cap.tags),
                    "version": cap.version,
                    "stability": cap.stability,
                })
            return sorted(out, key=lambda x: x["id"])

    def get(self, cap_id: str) -> Optional[Capability]:
        with self._lock:
            return self._caps.get(cap_id)

    def invoke(self, cap_id: str, arguments: Optional[dict] = None,
               *, allow_permissions: Optional[set] = None) -> dict:
        args = dict(arguments or {})
        with self._lock:
            cap = self._caps.get(cap_id)
        if cap is None:
            return {"ok": False, "error": "unknown capability: %s" % cap_id}
        if allow_permissions is not None and cap.permission not in allow_permissions:
            return {
                "ok": False,
                "error": "denied permission %s" % cap.permission,
                "capability": cap_id,
            }
        if cap.handler is None:
            return {"ok": False, "error": "no handler", "capability": cap_id}
        started = time.time()
        try:
            result = cap.handler(args)
            if not isinstance(result, dict):
                result = {"ok": True, "result": result}
        except Exception as exc:
            return {
                "ok": False,
                "error": "%s: %s" % (type(exc).__name__, exc),
                "capability": cap_id,
                "trace": traceback.format_exc()[-2000:],
            }
        text = str(result)
        if len(text) > self.result_limit:
            result = dict(result)
            result["_truncated"] = True
            result["_note"] = "result truncated for safety"
        result.setdefault("ok", True)
        result["_capability"] = cap_id
        result["_ms"] = int((time.time() - started) * 1000)
        return result


_BUS = CapabilityBus()


def bus() -> CapabilityBus:
    return _BUS


def register(plugin_id: str, caps: List[Capability]) -> None:
    _BUS.register(plugin_id, caps)


def unregister(plugin_id: str) -> None:
    _BUS.unregister(plugin_id)


def list_capabilities(**kwargs) -> List[dict]:
    return _BUS.list(**kwargs)


def invoke(cap_id: str, arguments: Optional[dict] = None, **kwargs) -> dict:
    return _BUS.invoke(cap_id, arguments, **kwargs)
