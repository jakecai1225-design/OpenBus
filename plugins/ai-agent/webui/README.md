# AI Agent webui (Vite + React + TypeScript)

Cursor-style workbench UI served by the Python bridge (`bridge/server.py`)
or the Node Mastra sidecar (`agent-ts`).

## Build

```bash
cd plugins/ai-agent/webui
npm install
npm run build
# output -> dist/ (bundled into .opk)
```

## Bridge contract (do not break)

- `GET /api/health`, `/api/settings`, `/api/snapshot`, `/api/attachments`, `/api/approve/pending`, `/api/tools`
- `PUT /api/settings`
- `POST /api/chat` → Vercel AI data stream (`0:` text, `9:` tool call, `a:` tool result, `3:` error, `d:` finish)
- `POST /api/approve`, `POST /api/tools/invoke`
- `DELETE /api/attachments/<id>`, `POST /api/attachments/clear`

HITL Approve/Reject buttons poll `/api/approve/pending` and POST `/api/approve`.
