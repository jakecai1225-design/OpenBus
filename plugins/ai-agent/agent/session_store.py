# -*- coding: utf-8 -*-
"""JSONL audit log. API keys are never written here."""

from __future__ import annotations

import json
import os
import time
import uuid


def agent_home() -> str:
    root = os.environ.get("OPENBUS_AI_HOME") or os.path.join(
        os.path.expanduser("~"), ".openbus", "ai-agent")
    os.makedirs(root, exist_ok=True)
    return root


def settings_path() -> str:
    return os.path.join(agent_home(), "settings.json")


def load_settings() -> dict:
    path = settings_path()
    if not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def save_settings(data: dict) -> str:
    path = settings_path()
    clean = dict(data)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(clean, f, indent=2, ensure_ascii=False)
    return path


class SessionStore:
    def __init__(self, session_id: str | None = None):
        self.session_id = session_id or uuid.uuid4().hex[:12]
        folder = os.path.join(agent_home(), "sessions")
        os.makedirs(folder, exist_ok=True)
        self.path = os.path.join(folder, self.session_id + ".jsonl")

    def append(self, record: dict) -> None:
        row = dict(record)
        row.setdefault("ts", time.time())
        row.setdefault("session", self.session_id)
        if "api_key" in row:
            row["api_key"] = "***"
        with open(self.path, "a", encoding="utf-8") as f:
            f.write(json.dumps(row, ensure_ascii=False) + "\n")

    def export_markdown(self, lines: list[str]) -> str:
        folder = os.path.dirname(self.path)
        path = os.path.join(folder, self.session_id + "-report.md")
        body = "# openbus AI Agent report\n\nSession: %s\n\n%s\n" % (
            self.session_id, "\n\n".join(lines))
        with open(path, "w", encoding="utf-8") as f:
            f.write(body)
        self.append({"kind": "report", "path": path})
        return path
