"""JSON session state under project_dir/plugins/<plugin_id>/ (or temp fallback)."""

from __future__ import annotations

import json
import os
import tempfile
from typing import Any, Optional


def _project_root() -> str:
    try:
        import sin

        path = sin.workspace.get_project_dir() or ""
        if path and os.path.isdir(path):
            return path
    except Exception:
        pass
    return ""


def state_dir(plugin_id: str) -> str:
    """Directory for this plugin's persisted JSON files."""
    root = _project_root()
    if root:
        d = os.path.join(root, "plugins", plugin_id)
    else:
        d = os.path.join(tempfile.gettempdir(), "openbus_plugins", plugin_id)
    os.makedirs(d, exist_ok=True)
    return d


def state_path(plugin_id: str, name: str = "state.json") -> str:
    return os.path.join(state_dir(plugin_id), name)


def save_state(plugin_id: str, data: Any, name: str = "state.json") -> str:
    path = state_path(plugin_id, name)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    return path


def load_state(
    plugin_id: str,
    name: str = "state.json",
    default: Optional[Any] = None,
) -> Any:
    path = state_path(plugin_id, name)
    if not os.path.isfile(path):
        return default
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return default


def clear_state(plugin_id: str, name: str = "state.json") -> bool:
    path = state_path(plugin_id, name)
    if os.path.isfile(path):
        try:
            os.remove(path)
            return True
        except OSError:
            return False
    return False
