# -*- coding: utf-8 -*-
"""Shared UI localization for Python plugin suites.

Catalogs: plugins/_shared/locales/<locale>.json (flat English-key → string).
Host notifies via JSON-RPC method ``setLanguage`` / ``set_language``.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Callable, Dict, List, Optional

_LOCALES_DIR = Path(__file__).resolve().parent / "locales"
_SUPPORTED = (
    "en",
    "zh_CN",
    "zh_TW",
    "es",
    "fr",
    "de",
    "ja",
    "pt_BR",
    "ru",
    "ko",
)

_language = "en"
_catalog: Dict[str, str] = {}
_callbacks: List[Callable[[str], None]] = []


def _normalize(locale: str) -> str:
    s = (locale or "en").strip().replace("-", "_")
    if s in _SUPPORTED:
        return s
    if s.startswith("zh"):
        if "TW" in s.upper() or "HK" in s.upper() or "Hant" in s:
            return "zh_TW"
        return "zh_CN"
    if s.startswith("pt"):
        return "pt_BR"
    lang = s.split("_", 1)[0]
    if lang in _SUPPORTED:
        return lang
    return "en"


def _load_catalog(locale: str) -> Dict[str, str]:
    path = _LOCALES_DIR / f"{locale}.json"
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            return {str(k): str(v) for k, v in data.items()}
    except Exception:
        pass
    return {}


def language() -> str:
    return _language


def set_language(locale: str) -> str:
    """Load catalog and notify listeners. Returns normalized locale."""
    global _language, _catalog
    loc = _normalize(locale)
    _language = loc
    _catalog = _load_catalog(loc) if loc != "en" else _load_catalog("en")
    if loc != "en" and not _catalog:
        _catalog = _load_catalog("en")
    for cb in list(_callbacks):
        try:
            cb(loc)
        except Exception:
            pass
    return loc


def on_language_changed(callback: Callable[[str], None]) -> None:
    if callback not in _callbacks:
        _callbacks.append(callback)


def off_language_changed(callback: Callable[[str], None]) -> None:
    try:
        _callbacks.remove(callback)
    except ValueError:
        pass


def t(key: str, default: Optional[str] = None) -> str:
    """Translate by English key; fallback to default or key itself."""
    if key in _catalog and _catalog[key]:
        return _catalog[key]
    if default is not None:
        return default
    return key


def _(text: str) -> str:
    """gettext-style helper: English source is both key and fallback."""
    return t(text, text)


# Bootstrap from env (host may set SIN_UI_LANGUAGE before activate)
_env = os.environ.get("SIN_UI_LANGUAGE") or os.environ.get("OPENBUS_UI_LANGUAGE")
if _env:
    set_language(_env)
else:
    set_language("en")
