# -*- coding: utf-8 -*-
"""Keep a thin re-export; prefer AgentRuntime for new code."""

from agent.orchestrator import Orchestrator  # noqa: F401

# Legacy hand-rolled loop remains for unit tests only.
# Production path: agent.agents_runtime.AgentRuntime + bridge + assistant-ui.
