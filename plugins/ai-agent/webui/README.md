# AI Agent webui

Static chat client served by the Python stdlib bridge (`bridge/server.py`).

## Runtime

Ship `dist/` inside the `.opk`. No Node required at runtime.

Current `dist/` is a lightweight Cursor-style shell that speaks the same
`/api/chat` AI-SDK data stream the bridge already emits. It is enough for
path A (browser) and path B (Edge `--app` embed).

## Optional: rebuild with assistant-ui (later)

When MSYS2 `nodejs`/`npm` are installed:

```bash
# from MSYS2 UCRT64
cd plugins/ai-agent/webui
# scaffold Vite + @assistant-ui/react + @assistant-ui/react-ai-sdk
# npm install && npm run build
# output must land in dist/ (index.html + assets)
```

Keep the bridge contract:

- `GET /api/health`, `/api/settings`, `/api/snapshot`, `/api/attachments`
- `PUT /api/settings`
- `POST /api/chat` → Vercel AI data stream (`0:` text, `9:` tool call, `a:` tool result)
- `POST /api/approve`, `GET /api/approve/pending`
