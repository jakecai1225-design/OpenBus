# -*- coding: utf-8 -*-
"""sin.ai — attach context to the AI Agent chat (Add to AI Chat)."""

from __future__ import annotations

from ._transport import send_notification


class _AiApi:
    def attach(self, attachment: dict | list | None = None, **kwargs) -> None:
        """Push attachments into the AI Context Inbox and open the chat."""
        items = []
        if isinstance(attachment, list):
            items = list(attachment)
        elif isinstance(attachment, dict):
            items = [attachment]
        elif kwargs:
            items = [kwargs]

        pushed = False
        try:
            from _shared import ai_attach
            for item in items:
                if item.get("kind") == "trace_frames" and "frames" in item:
                    ai_attach.attach_frames(
                        item.get("frames") or [],
                        title=item.get("title") or "")
                elif item.get("kind") == "eds":
                    ai_attach.attach_eds(
                        item.get("path") or "",
                        summary=item.get("summary"),
                        title=item.get("title") or "")
                else:
                    ai_attach.attach(item)
            pushed = True
        except Exception:
            pushed = False

        if not pushed:
            send_notification("ai.attach", {"attachments": items})
        try:
            from .commands import commands
            commands.execute("aiAgent.open")
        except Exception:
            send_notification("executeCommand", {"id": "aiAgent.open"})

    def attach_frames(self, frames: list, title: str = "") -> None:
        self.attach({
            "kind": "trace_frames",
            "title": title or ("Trace ×%d" % len(frames)),
            "frames": frames,
        })

    def attach_eds(self, path: str, summary: dict | None = None,
                   title: str = "") -> None:
        self.attach({
            "kind": "eds",
            "title": title or "",
            "path": path,
            "summary": summary or {},
        })


ai = _AiApi()
