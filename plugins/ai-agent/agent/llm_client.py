# -*- coding: utf-8 -*-
"""OpenAI-compatible chat client. Ollama uses the same /v1 path."""

from __future__ import annotations

import json
import urllib.error
import urllib.request


class LLMResponse:
    def __init__(self, content: str = "", tool_calls: list | None = None):
        self.content = content or ""
        self.tool_calls = tool_calls or []


class LLMError(RuntimeError):
    pass


class ScriptedLLM:
    """Test double: returns a fixed sequence of responses."""

    def __init__(self, script: list[dict]):
        self.script = list(script)
        self.calls = 0

    def chat(self, messages, tools=None) -> LLMResponse:
        self.calls += 1
        if not self.script:
            return LLMResponse(content="(script exhausted)")
        item = self.script.pop(0)
        return LLMResponse(
            content=item.get("content") or "",
            tool_calls=item.get("tool_calls") or [],
        )


class LLMClient:
    def __init__(self, base_url: str, api_key: str, model: str,
                 temperature: float = 0.2, max_tokens: int = 1200,
                 timeout: float = 60.0):
        self.base_url = (base_url or "").rstrip("/")
        self.api_key = api_key or ""
        self.model = model or ""
        self.temperature = temperature
        self.max_tokens = max_tokens
        self.timeout = timeout

    def chat(self, messages, tools=None) -> LLMResponse:
        if not self.base_url or not self.model:
            raise LLMError("Set base URL and model before sending")
        if not self.api_key:
            raise LLMError("Set an API key (use any placeholder for local Ollama)")
        url = self.base_url + "/chat/completions"
        body = {
            "model": self.model,
            "messages": messages,
            "temperature": self.temperature,
            "max_tokens": self.max_tokens,
        }
        if tools:
            body["tools"] = tools
            body["tool_choice"] = "auto"
        data = json.dumps(body).encode("utf-8")
        req = urllib.request.Request(url, data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Authorization", "Bearer " + self.api_key)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                payload = json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")[:400]
            raise LLMError("LLM HTTP %s: %s" % (e.code, detail)) from e
        except urllib.error.URLError as e:
            raise LLMError("LLM connection failed: %s" % e.reason) from e
        return _parse_payload(payload)


def _parse_payload(payload: dict) -> LLMResponse:
    choices = payload.get("choices") or []
    if not choices:
        raise LLMError("LLM response had no choices")
    msg = choices[0].get("message") or {}
    calls = []
    for raw in msg.get("tool_calls") or []:
        fn = raw.get("function") or {}
        args = fn.get("arguments") or {}
        if isinstance(args, str):
            try:
                args = json.loads(args) if args.strip() else {}
            except json.JSONDecodeError:
                args = {}
        if not isinstance(args, dict):
            args = {}
        calls.append({
            "id": raw.get("id") or ("call_%d" % len(calls)),
            "name": fn.get("name") or "",
            "arguments": args,
        })
    return LLMResponse(content=msg.get("content") or "", tool_calls=calls)
