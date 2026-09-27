/**
 * Mastra integration point for openbus AI Agent.
 *
 * V1 runtime (server.ts): Vercel AI SDK `streamText` + tools — the same streaming
 * edge Mastra uses. Tools always execute in Python via OPENBUS_TOOL_BRIDGE.
 *
 * Next (when `@mastra/core` is installed in this package):
 *
 *   import { Agent } from '@mastra/core/agent'
 *   export function createOpenbusAgent({ model, tools, instructions }) {
 *     return new Agent({
 *       name: 'openbus-analyst',
 *       instructions,
 *       model,
 *       tools,
 *     })
 *   }
 *
 * Phase B: wire `@mastra/memory` for session memory; keep project Knowledge
 * as files under ~/.openbus/ai-agent/knowledge/ for audit/delete.
 * Phase C: Mastra MCP client/server sharing ToolRegistry with Python.
 */

export const RUNTIME = 'ai-sdk-tool-loop' as const
export const MASTRA_EDGE = true
export const TOOL_EXECUTOR = 'python-bridge' as const
