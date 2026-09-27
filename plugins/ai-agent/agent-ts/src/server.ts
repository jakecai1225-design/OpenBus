/**
 * Node sidecar for openbus AI Agent.
 * Vercel AI SDK tool-loop (Mastra-compatible streaming edge).
 * Tools execute via Python bridge POST /api/tools/invoke.
 */
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http'
import { createOpenAICompatible } from '@ai-sdk/openai-compatible'
import { streamText, tool, jsonSchema, type CoreMessage } from 'ai'

const HOST = process.env.HOST || '127.0.0.1'
const PORT = Number(process.env.PORT || 0) || 8787
const TOOL_BRIDGE = (process.env.OPENBUS_TOOL_BRIDGE || 'http://127.0.0.1:8765').replace(/\/$/, '')

type BridgeTool = {
  name: string
  description?: string
  parameters?: Record<string, unknown>
}

type Settings = {
  base_url?: string
  api_key?: string
  model?: string
  policy?: string
  role?: string
}

async function bridgeJson(path: string, init?: RequestInit) {
  const res = await fetch(`${TOOL_BRIDGE}${path}`, {
    ...init,
    headers: {
      'Content-Type': 'application/json',
      ...(init?.headers || {}),
    },
  })
  if (!res.ok) {
    const text = await res.text()
    throw new Error(`bridge ${path} ${res.status}: ${text}`)
  }
  return res.json()
}

async function loadSettings(): Promise<Settings> {
  try {
    return (await bridgeJson('/api/settings')) as Settings
  } catch {
    return {
      base_url: 'http://127.0.0.1:11434/v1',
      model: 'llama3.2',
      policy: 'readonly',
    }
  }
}

async function loadTools(): Promise<BridgeTool[]> {
  try {
    const data = (await bridgeJson('/api/tools')) as { tools?: BridgeTool[] }
    return data.tools || []
  } catch {
    return []
  }
}

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    req.on('data', (c) => chunks.push(Buffer.from(c)))
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    req.on('error', reject)
  })
}

function cors(res: ServerResponse) {
  res.setHeader('Access-Control-Allow-Origin', '*')
  res.setHeader('Access-Control-Allow-Methods', 'GET,POST,PUT,DELETE,OPTIONS')
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type')
}

function sendJson(res: ServerResponse, code: number, obj: unknown) {
  cors(res)
  const body = JSON.stringify(obj)
  res.writeHead(code, {
    'Content-Type': 'application/json',
    'Content-Length': Buffer.byteLength(body),
  })
  res.end(body)
}

function extractUserText(body: Record<string, unknown>): string {
  const messages = body.messages
  if (Array.isArray(messages) && messages.length) {
    const last = messages[messages.length - 1] as Record<string, unknown>
    const parts = last.parts
    if (Array.isArray(parts)) {
      const texts = parts
        .filter((p) => p && typeof p === 'object' && (p as { type?: string }).type === 'text')
        .map((p) => String((p as { text?: string }).text || ''))
      if (texts.length) return texts.join('')
    }
    if (typeof last.content === 'string') return last.content
  }
  return String(body.input || body.prompt || '').trim()
}

async function proxyOrLocal(
  req: IncomingMessage,
  res: ServerResponse,
  path: string,
) {
  const url = `${TOOL_BRIDGE}${path}`
  const headers: Record<string, string> = {}
  const ct = req.headers['content-type']
  if (ct) headers['Content-Type'] = String(ct)
  let body: string | undefined
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    body = await readBody(req)
  }
  try {
    const upstream = await fetch(url, {
      method: req.method,
      headers,
      body: body && body.length ? body : undefined,
    })
    const buf = Buffer.from(await upstream.arrayBuffer())
    cors(res)
    res.writeHead(upstream.status, {
      'Content-Type': upstream.headers.get('content-type') || 'application/json',
      'Content-Length': buf.length,
    })
    res.end(buf)
  } catch (e) {
    sendJson(res, 502, { error: String(e) })
  }
}

async function handleChat(req: IncomingMessage, res: ServerResponse) {
  const raw = await readBody(req)
  let body: Record<string, unknown> = {}
  try {
    body = JSON.parse(raw || '{}')
  } catch {
    body = {}
  }
  const userText = extractUserText(body)
  if (!userText) {
    sendJson(res, 400, { error: 'empty message' })
    return
  }

  const settings = await loadSettings()
  const baseURL = (settings.base_url || 'http://127.0.0.1:11434/v1').replace(/\/$/, '')
  const modelName = settings.model || 'llama3.2'
  const apiKey = settings.api_key || 'ollama'

  const provider = createOpenAICompatible({
    name: 'openbus',
    baseURL,
    apiKey,
  })

  const bridgeTools = await loadTools()
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const tools: Record<string, any> = {}
  for (const t of bridgeTools) {
    if (!t.name) continue
    const toolName = t.name
    const params = (t.parameters && typeof t.parameters === 'object'
      ? t.parameters
      : { type: 'object', properties: {} }) as Record<string, unknown>
    tools[toolName] = tool({
      description: t.description || toolName,
      parameters: jsonSchema(params),
      execute: async (args: unknown) => {
        return bridgeJson('/api/tools/invoke', {
          method: 'POST',
          body: JSON.stringify({ name: toolName, arguments: args || {} }),
        })
      },
    })
  }

  const system = [
    'You are the openbus AI Agent — a bus analysis collaborator.',
    `Policy level: ${settings.policy || 'readonly'}. Role: ${settings.role || 'Analyst'}.`,
    'Use tools for Trace/DBC/diagnostics facts. Do not invent bus data.',
    'Write tools require user approval via the host policy gate.',
  ].join('\n')

  const messages: CoreMessage[] = [
    { role: 'system', content: system },
    { role: 'user', content: userText },
  ]

  cors(res)
  res.writeHead(200, {
    'Content-Type': 'text/plain; charset=utf-8',
    'Cache-Control': 'no-cache',
    'X-Vercel-AI-Data-Stream': 'v1',
  })

  const line = (prefix: string, payload: unknown) => {
    res.write(`${prefix}:${JSON.stringify(payload)}\n`)
  }

  try {
    const result = streamText({
      model: provider(modelName),
      messages,
      tools: Object.keys(tools).length ? tools : undefined,
      maxSteps: 6,
      temperature: 0.2,
    })

    for await (const part of result.fullStream) {
      const p = part as { type: string; textDelta?: string; toolCallId?: string; toolName?: string; args?: unknown; result?: unknown; error?: unknown }
      if (p.type === 'text-delta' && typeof p.textDelta === 'string') {
        line('0', p.textDelta)
      } else if (p.type === 'tool-call') {
        line('9', {
          toolCallId: p.toolCallId,
          toolName: p.toolName,
          args: p.args,
        })
      } else if (p.type === 'tool-result') {
        line('a', {
          toolCallId: p.toolCallId,
          result: p.result,
        })
      } else if (p.type === 'error') {
        line('3', String(p.error))
      }
    }
    line('d', { finishReason: 'stop', runtime: 'mastra-ai-sdk' })
  } catch (e) {
    const err = e as Error
    line('3', `${err.name || 'Error'}: ${err.message || e}`)
    line('d', { finishReason: 'error' })
  }
  res.write('data: [DONE]\n\n')
  res.end()
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url || '/', `http://${HOST}:${PORT}`)
  const path = url.pathname

  if (req.method === 'OPTIONS') {
    cors(res)
    res.writeHead(204)
    res.end()
    return
  }

  if (path === '/api/health' && req.method === 'GET') {
    sendJson(res, 200, {
      ok: true,
      runtime: 'agent-ts',
      framework: 'ai-sdk+mastra-edge',
      toolBridge: TOOL_BRIDGE,
    })
    return
  }

  if (path === '/api/chat' && req.method === 'POST') {
    await handleChat(req, res)
    return
  }

  await proxyOrLocal(req, res, path === '/' ? '/index.html' : path)
})

server.listen(PORT, HOST, () => {
  console.log(`[agent-ts] listening http://${HOST}:${PORT} bridge=${TOOL_BRIDGE}`)
})
