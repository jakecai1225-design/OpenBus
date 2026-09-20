# -*- coding: utf-8 -*-
"""Tool loop: model, tools, audit, HITL approval."""

from __future__ import annotations

import json
import time

from agent.prompts import system_prompt


def _tool_message(call_id: str, payload: dict) -> dict:
    text = json.dumps(payload, ensure_ascii=False)
    if len(text) > 12000:
        text = text[:12000] + "...(truncated)"
    return {"role": "tool", "tool_call_id": call_id, "content": text}


class Orchestrator:
    def __init__(self, llm, registry, store, role: str = "Analyst", max_steps: int = 6):
        self.llm = llm
        self.registry = registry
        self.store = store
        self.role = role
        self.max_steps = max_steps
        self.inject_activity_snapshot = True
        self.messages = [self._system_message()]

    def _policy_level(self) -> str:
        return getattr(getattr(self.registry, "policy", None), "level", "readonly")

    def _activity_block(self) -> str:
        if not self.inject_activity_snapshot:
            return ""
        try:
            from agent import activity_snapshot
            return activity_snapshot.format_for_prompt()
        except Exception:
            try:
                from _shared import activity_snapshot
                return activity_snapshot.format_for_prompt()
            except Exception:
                return ""

    def _system_message(self) -> dict:
        return {
            "role": "system",
            "content": system_prompt(
                self.role, self._policy_level(), self._activity_block()),
        }

    def set_role(self, role: str) -> None:
        self.role = role
        self._refresh_system()

    def refresh_policy_prompt(self) -> None:
        self._refresh_system()

    def _refresh_system(self) -> None:
        msg = self._system_message()
        if self.messages and self.messages[0].get("role") == "system":
            self.messages[0] = msg
        else:
            self.messages.insert(0, msg)

    def reset(self) -> None:
        self.messages = [self._system_message()]

    def run(self, user_text: str, on_step=None) -> str:
        self._refresh_system()
        self.messages.append({"role": "user", "content": user_text})
        self.store.append({
            "kind": "user",
            "role": self.role,
            "policy": self._policy_level(),
            "text": user_text,
        })
        tools = self.registry.schema()
        for _step in range(self.max_steps):
            resp = self.llm.chat(self.messages, tools)
            if not resp.tool_calls:
                final = (resp.content or "").strip() or "(empty response)"
                self.messages.append({"role": "assistant", "content": final})
                self.store.append({"kind": "assistant", "text": final})
                return final

            assistant = {
                "role": "assistant",
                "content": resp.content or "",
                "tool_calls": [],
            }
            for call in resp.tool_calls:
                assistant["tool_calls"].append({
                    "id": call.get("id"),
                    "type": "function",
                    "function": {
                        "name": call.get("name"),
                        "arguments": json.dumps(call.get("arguments") or {}),
                    },
                })
            self.messages.append(assistant)

            for call in resp.tool_calls:
                name = call.get("name") or ""
                args = call.get("arguments") or {}
                started = time.time()
                result = self.registry.invoke(name, args)
                elapsed_ms = int((time.time() - started) * 1000)
                truncated = bool(
                    isinstance(result, dict)
                    and (
                        result.get("_truncated")
                        or (isinstance(result.get("data"), dict)
                            and result["data"].get("_truncated"))
                    )
                )
                summary = json.dumps(result, ensure_ascii=False)
                if len(summary) > 500:
                    summary = summary[:500] + "...(truncated)"
                self.store.append({
                    "kind": "tool",
                    "name": name,
                    "args": args,
                    "ok": bool(result.get("ok")),
                    "summary": summary,
                    "ms": elapsed_ms,
                    "truncated": truncated,
                })
                if on_step:
                    flag = "ok" if result.get("ok") else "err"
                    trunc = " trunc" if truncated else ""
                    on_step("%s %s %dms%s" % (flag, name, elapsed_ms, trunc))
                self.messages.append(_tool_message(call.get("id") or name, result))

        final = "Stopped: tool-loop limit reached without a final answer."
        self.messages.append({"role": "assistant", "content": final})
        self.store.append({"kind": "assistant", "text": final})
        return final
