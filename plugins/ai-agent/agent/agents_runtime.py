# -*- coding: utf-8 -*-
"""Agent runtime: prefer OpenAI Agents SDK; fallback to local Orchestrator.

MSYS2 MinGW Python often cannot pip-install openai-agents (native wheels).
Fallback keeps the workbench usable with the existing tool loop.
"""

from __future__ import annotations

import json
import threading
from typing import Any, Dict, Iterator, List, Optional

from agent.llm_client import LLMClient
from agent.orchestrator import Orchestrator
from agent.prompts import system_prompt
from agent.session_store import SessionStore, load_settings
from tools.host import SinHost
from tools.policy import Policy
from tools.registry import ToolRegistry

try:
    from _shared import activity_snapshot, ai_attach
except ImportError:
    activity_snapshot = None  # type: ignore
    ai_attach = None  # type: ignore

_HAS_AGENTS = False
try:
    from agents import Agent, Runner  # noqa: F401
    from agents.tool import FunctionTool  # noqa: F401
    from agents.models.openai_chatcompletions import OpenAIChatCompletionsModel
    _HAS_AGENTS = True
except ImportError:
    _HAS_AGENTS = False


class AgentRuntimeError(RuntimeError):
    pass


class ApprovalGate:
    """Thread-safe HITL gate shared with /api/approve."""

    def __init__(self):
        self._cond = threading.Condition()
        self._pending: Optional[dict] = None
        self._answer: Optional[bool] = None

    def request(self, info: dict, timeout: float = 120.0) -> bool:
        with self._cond:
            self._pending = dict(info)
            self._answer = None
            self._cond.notify_all()
            deadline = timeout
            while self._answer is None and deadline > 0:
                self._cond.wait(timeout=0.2)
                deadline -= 0.2
            ok = bool(self._answer)
            self._pending = None
            self._answer = None
            return ok

    def pending(self) -> Optional[dict]:
        with self._cond:
            return dict(self._pending) if self._pending else None

    def resolve(self, approved: bool) -> bool:
        with self._cond:
            if self._pending is None:
                return False
            self._answer = bool(approved)
            self._cond.notify_all()
            return True


class AgentRuntime:
    def __init__(self):
        self.store = SessionStore()
        self.policy = Policy("readonly")
        self.host = SinHost()
        self.approval = ApprovalGate()
        self.registry = ToolRegistry(
            self.host, self.policy, approval_fn=self._approve)
        self.inject_activity_snapshot = True
        self._llm = LLMClient("", "", "")
        self._orch = Orchestrator(self._llm, self.registry, self.store)
        self._settings = load_settings()
        self._apply_settings(self._settings)

    def _approve(self, info: dict) -> bool:
        return self.approval.request(info)

    def _apply_settings(self, data: dict) -> None:
        self._settings = dict(data or {})
        level = self._settings.get("policy") or "readonly"
        try:
            self.policy.set_level(level)
        except ValueError:
            self.policy.set_level("readonly")
        inject = self._settings.get("inject_activity_snapshot")
        self.inject_activity_snapshot = True if inject is None else bool(inject)
        self._llm.base_url = (self._settings.get("base_url") or "").rstrip("/")
        self._llm.api_key = self._settings.get("api_key") or ""
        self._llm.model = self._settings.get("model") or ""
        role = self._settings.get("role") or "Analyst"
        self._orch.set_role(role)
        self._orch.inject_activity_snapshot = self.inject_activity_snapshot
        self._orch.refresh_policy_prompt()

    def reload_settings(self) -> dict:
        self._apply_settings(load_settings())
        return dict(self._settings)

    def settings(self) -> dict:
        out = dict(self._settings)
        out.setdefault("provider", "Ollama")
        out.setdefault("base_url", "http://127.0.0.1:11434/v1")
        out.setdefault("model", "llama3.2")
        out.setdefault("policy", self.policy.level)
        out.setdefault("role", "Analyst")
        out["inject_activity_snapshot"] = self.inject_activity_snapshot
        out["agents_sdk"] = _HAS_AGENTS
        return out

    def _activity_block(self) -> str:
        if not self.inject_activity_snapshot or activity_snapshot is None:
            return ""
        try:
            return activity_snapshot.format_for_prompt()
        except Exception:
            return ""

    def _attachment_block(self) -> str:
        if ai_attach is None:
            return ""
        try:
            items = ai_attach.get_inbox().items()
            if not items:
                return ""
            return ai_attach.resolve(items, budget=12000)
        except Exception:
            return ""

    def _compose_user(self, user_text: str) -> str:
        att = self._attachment_block()
        if not att:
            return user_text
        return "%s\n\n## Attached context\n%s" % (user_text, att)

    def run_streamed_sync(self, user_text: str) -> Iterator[Dict[str, Any]]:
        """Yield AI-SDK-style events for the HTTP bridge (sync)."""
        self.reload_settings()
        text = self._compose_user(user_text)
        if _HAS_AGENTS:
            try:
                yield from self._run_agents_sdk(text)
                return
            except Exception as e:
                yield {
                    "type": "text-delta",
                    "id": "msg_assistant",
                    "delta": "\n[agents-sdk fallback: %s]\n" % e,
                }
        yield from self._run_orchestrator(text)

    def _run_orchestrator(self, user_text: str) -> Iterator[Dict[str, Any]]:
        yield {"type": "text-start", "id": "msg_assistant"}
        try:
            step_lines: List[str] = []

            def _step(line: str):
                step_lines.append(line)

            answer = self._orch.run(user_text, on_step=_step)
            for line in step_lines:
                parts = line.split()
                name = parts[1] if len(parts) > 1 else line
                yield {
                    "type": "tool-input-available",
                    "toolCallId": name,
                    "toolName": name,
                    "input": {"status": parts[0] if parts else ""},
                }
                yield {
                    "type": "tool-output-available",
                    "toolCallId": name,
                    "output": line,
                }
            if answer:
                # Chunk for progressive feel
                chunk = 48
                for i in range(0, len(answer), chunk):
                    yield {
                        "type": "text-delta",
                        "id": "msg_assistant",
                        "delta": answer[i:i + chunk],
                    }
            yield {"type": "text-end", "id": "msg_assistant"}
            yield {"type": "finish", "finishReason": "stop"}
        except Exception as e:
            err = "%s: %s" % (type(e).__name__, e)
            yield {
                "type": "text-delta",
                "id": "msg_assistant",
                "delta": "\n\n[error] " + err,
            }
            yield {"type": "text-end", "id": "msg_assistant"}
            yield {"type": "error", "errorText": err}

    def _run_agents_sdk(self, user_text: str) -> Iterator[Dict[str, Any]]:
        import asyncio
        from openai import AsyncOpenAI
        from agents import Agent, Runner, ModelSettings
        from agents.tool import FunctionTool
        from agents.models.openai_chatcompletions import OpenAIChatCompletionsModel

        base = (self._settings.get("base_url") or "").rstrip("/")
        key = self._settings.get("api_key") or "ollama"
        model_name = self._settings.get("model") or ""
        if not base or not model_name:
            raise AgentRuntimeError(
                "Configure base URL and model in Settings before chatting")

        client = AsyncOpenAI(base_url=base, api_key=key)
        model = OpenAIChatCompletionsModel(
            model=model_name, openai_client=client)
        role = self._settings.get("role") or "Analyst"
        instructions = system_prompt(
            role, self.policy.level, self._activity_block())

        tools = []
        for item in self.registry.schema():
            fn = item.get("function") or {}
            name = fn.get("name") or ""
            if not name:
                continue
            description = fn.get("description") or name
            parameters = fn.get("parameters") or {
                "type": "object", "properties": {}}
            if isinstance(parameters, dict) and "additionalProperties" not in parameters:
                parameters = dict(parameters)
                parameters["additionalProperties"] = False

            async def _on_invoke(ctx, raw_args: str, _name=name) -> str:
                try:
                    args = json.loads(raw_args) if raw_args else {}
                except json.JSONDecodeError:
                    args = {}
                if not isinstance(args, dict):
                    args = {}
                result = self.registry.invoke(_name, args)
                text = json.dumps(result, ensure_ascii=False)
                if len(text) > 12000:
                    text = text[:12000] + "...(truncated)"
                return text

            tools.append(FunctionTool(
                name=name,
                description=description,
                params_json_schema=parameters,
                on_invoke_tool=_on_invoke,
                strict_json_schema=False,
            ))

        agent = Agent(
            name="openbus-analyst",
            instructions=instructions,
            model=model,
            model_settings=ModelSettings(temperature=0.2, max_tokens=1200),
            tools=tools,
        )

        async def _collect():
            out: List[dict] = []
            out.append({"type": "text-start", "id": "msg_assistant"})
            result = Runner.run_streamed(agent, user_text)
            final_parts: List[str] = []
            async for event in result.stream_events():
                et = getattr(event, "type", "") or ""
                if et == "raw_response_event":
                    data = getattr(event, "data", None)
                    delta = getattr(data, "delta", None) if data is not None else None
                    if isinstance(delta, str) and delta:
                        final_parts.append(delta)
                        out.append({
                            "type": "text-delta",
                            "id": "msg_assistant",
                            "delta": delta,
                        })
                elif et == "run_item_stream_event":
                    item = getattr(event, "item", None)
                    item_type = getattr(item, "type", "") if item else ""
                    if item_type == "tool_call_item":
                        raw = getattr(item, "raw_item", None)
                        tname = getattr(raw, "name", None) or "tool"
                        call_id = getattr(raw, "call_id", None) or tname
                        args = getattr(raw, "arguments", {}) or {}
                        if isinstance(args, str):
                            try:
                                args = json.loads(args) if args.strip() else {}
                            except json.JSONDecodeError:
                                args = {"raw": args}
                        out.append({
                            "type": "tool-input-available",
                            "toolCallId": str(call_id),
                            "toolName": str(tname),
                            "input": args,
                        })
                    elif item_type == "tool_call_output_item":
                        out.append({
                            "type": "tool-output-available",
                            "toolCallId": getattr(item, "call_id", "") or "tool",
                            "output": getattr(item, "output", ""),
                        })
            if not final_parts:
                try:
                    final = (result.final_output or "").strip()
                except Exception:
                    final = ""
                if final:
                    out.append({
                        "type": "text-delta",
                        "id": "msg_assistant",
                        "delta": final,
                    })
            answer = "".join(final_parts).strip()
            if answer:
                self.store.append({"kind": "assistant", "text": answer})
            out.append({"type": "text-end", "id": "msg_assistant"})
            out.append({"type": "finish", "finishReason": "stop"})
            return out

        events = asyncio.run(_collect())
        for ev in events:
            yield ev


_RUNTIME: Optional[AgentRuntime] = None
_LOCK = threading.Lock()


def get_runtime() -> AgentRuntime:
    global _RUNTIME
    with _LOCK:
        if _RUNTIME is None:
            _RUNTIME = AgentRuntime()
        return _RUNTIME


def reset_runtime() -> None:
    global _RUNTIME
    with _LOCK:
        _RUNTIME = None
