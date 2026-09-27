# agent-ts — Mastra / AI SDK sidecar

Node.js agent runtime for openbus AI Agent.

- **Framework**: Vercel AI SDK `streamText` tool-loop (Mastra-compatible edge; same data stream as the Python bridge)
- **Tools**: proxied to Python `POST {OPENBUS_TOOL_BRIDGE}/api/tools/invoke`
- **UI**: static files proxied from the Python bridge `webui/dist`

## Build

Requires Node.js >= 20.

```bash
cd plugins/ai-agent/agent-ts
npm install
npm run build
```

## Run (usually started by ChatWindow)

```bash
set OPENBUS_TOOL_BRIDGE=http://127.0.0.1:<python-bridge-port>
set PORT=8787
npm start
```

When the sidecar is healthy, `ChatWindow` points WebView2 at this port.
Python Orchestrator remains the fallback if Node is missing.
