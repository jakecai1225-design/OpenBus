# -*- coding: utf-8 -*-
"""Tool loop: model, tools, audit."""

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
        self.messages = [{"role": "system", "content": system_prompt(role)}]

    def set_role(self, role: str) -> None:
        self.role = role
        if self.messages and self.messages[0].get("role") == "system":
            self.messages[0]["content"] = system_prompt(role)
        else:
            self.messages.insert(0, {"role": "system", "content": system_prompt(role)})

    def reset(self) -> None:
        self.messages = [{"role": "system", "content": system_prompt(self.role)}]

    def run(self, user_text: str, on_step=None) -> str:
        self.messages.append({"role": "user", "content": user_text})
        self.store.append({"kind": "user", "role": self.role, "text": user_text})
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
                })
                if on_step:
                    flag = "ok" if result.get("ok") else "err"
                    on_step("%s %s %dms" % (flag, name, elapsed_ms))
                self.messages.append(_tool_message(call.get("id") or name, result))

        final = "Stopped: tool-loop limit reached without a final answer."
        self.messages.append({"role": "assistant", "content": final})
        self.store.append({"kind": "assistant", "text": final})
        return final
