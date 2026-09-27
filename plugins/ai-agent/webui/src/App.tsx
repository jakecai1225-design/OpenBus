import { useCallback, useEffect, useRef, useState } from 'react'

type Attachment = { id: string; kind: string; title: string; preview?: string }
type ChatMsg =
  | { id: string; role: 'user' | 'assistant'; text: string }
  | { id: string; role: 'tool'; text: string }

type Settings = {
  provider?: string
  base_url?: string
  model?: string
  api_key?: string
  policy?: string
  role?: string
  inject_activity_snapshot?: boolean
  agents_sdk?: boolean
}

type Pending = { tool?: string; arguments?: unknown; policy?: string } | null

function uid() {
  return Math.random().toString(36).slice(2, 10)
}

async function readAiSdkStream(
  res: Response,
  onText: (delta: string) => void,
  onTool: (line: string) => void,
) {
  const reader = res.body?.getReader()
  if (!reader) throw new Error('No response body')
  const dec = new TextDecoder()
  let buf = ''
  while (true) {
    const { done, value } = await reader.read()
    if (done) break
    buf += dec.decode(value, { stream: true })
    const lines = buf.split('\n')
    buf = lines.pop() || ''
    for (const line of lines) {
      if (!line || line.startsWith('data:')) continue
      const colon = line.indexOf(':')
      if (colon < 0) continue
      const prefix = line.slice(0, colon)
      const payload = line.slice(colon + 1)
      try {
        if (prefix === '0') {
          const text = JSON.parse(payload)
          if (typeof text === 'string') onText(text)
        } else if (prefix === '9') {
          const obj = JSON.parse(payload)
          onTool(`call ${obj.toolName || '?'} ${JSON.stringify(obj.args || {})}`)
        } else if (prefix === 'a') {
          const obj = JSON.parse(payload)
          const r = typeof obj.result === 'string' ? obj.result : JSON.stringify(obj.result)
          onTool(`result ${r.slice(0, 400)}`)
        } else if (prefix === '3') {
          const err = JSON.parse(payload)
          onText(`\n[error] ${err}`)
        }
      } catch {
        /* ignore malformed chunk */
      }
    }
  }
}

export default function App() {
  const [messages, setMessages] = useState<ChatMsg[]>([])
  const [input, setInput] = useState('')
  const [busy, setBusy] = useState(false)
  const [health, setHealth] = useState(false)
  const [settings, setSettings] = useState<Settings>({})
  const [attachments, setAttachments] = useState<Attachment[]>([])
  const [snapshot, setSnapshot] = useState('')
  const [pending, setPending] = useState<Pending>(null)
  const bottomRef = useRef<HTMLDivElement>(null)

  const refreshMeta = useCallback(async () => {
    try {
      const [h, s, a, snap, ap] = await Promise.all([
        fetch('/api/health').then((r) => r.json()),
        fetch('/api/settings').then((r) => r.json()),
        fetch('/api/attachments').then((r) => r.json()),
        fetch('/api/snapshot').then((r) => r.json()),
        fetch('/api/approve/pending').then((r) => r.json()),
      ])
      setHealth(!!h.ok)
      setSettings(s)
      setAttachments(a.items || [])
      setSnapshot(snap.text || '')
      setPending(ap.pending || null)
    } catch {
      setHealth(false)
    }
  }, [])

  useEffect(() => {
    refreshMeta()
    const t = setInterval(refreshMeta, 2000)
    return () => clearInterval(t)
  }, [refreshMeta])

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages, busy])

  async function saveSettings(patch: Partial<Settings>) {
    const next = { ...settings, ...patch }
    setSettings(next)
    await fetch('/api/settings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(patch),
    })
  }

  async function removeAttachment(id: string) {
    await fetch(`/api/attachments/${id}`, { method: 'DELETE' })
    refreshMeta()
  }

  async function clearAttachments() {
    await fetch('/api/attachments/clear', { method: 'POST' })
    refreshMeta()
  }

  async function resolveApprove(approved: boolean) {
    await fetch('/api/approve', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ approved }),
    })
    setPending(null)
    refreshMeta()
  }

  async function send() {
    const text = input.trim()
    if (!text || busy) return
    setInput('')
    const userId = uid()
    const asstId = uid()
    setMessages((m) => [
      ...m,
      { id: userId, role: 'user', text },
      { id: asstId, role: 'assistant', text: '' },
    ])
    setBusy(true)
    try {
      const res = await fetch('/api/chat', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          messages: [
            {
              role: 'user',
              parts: [{ type: 'text', text }],
            },
          ],
        }),
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      await readAiSdkStream(
        res,
        (delta) => {
          setMessages((m) =>
            m.map((msg) =>
              msg.id === asstId && msg.role === 'assistant'
                ? { ...msg, text: msg.text + delta }
                : msg,
            ),
          )
        },
        (toolLine) => {
          setMessages((m) => [...m, { id: uid(), role: 'tool', text: toolLine }])
        },
      )
    } catch (e) {
      const err = e instanceof Error ? e.message : String(e)
      setMessages((m) =>
        m.map((msg) =>
          msg.id === asstId && msg.role === 'assistant'
            ? { ...msg, text: (msg.text || '') + `\n[error] ${err}` }
            : msg,
        ),
      )
    } finally {
      setBusy(false)
      refreshMeta()
    }
  }

  return (
    <div className="app">
      <div className="main">
        <div className="topbar">
          <span className={`status-dot ${health ? '' : 'off'}`} />
          <h1>openbus AI Agent</h1>
          <span style={{ color: 'var(--muted)', fontSize: 11 }}>
            {settings.model || 'no model'} · {settings.policy || 'readonly'}
          </span>
        </div>

        {(attachments.length > 0 || snapshot) && (
          <div className="chip-row">
            {attachments.map((a) => (
              <span key={a.id} className="chip" title={a.preview || a.kind}>
                {a.title || a.kind}
                <button type="button" onClick={() => removeAttachment(a.id)} aria-label="Remove">
                  ×
                </button>
              </span>
            ))}
            {attachments.length > 0 && (
              <button type="button" className="btn" onClick={clearAttachments}>
                Clear
              </button>
            )}
          </div>
        )}

        <div className="messages">
          {messages.length === 0 && (
            <div className="msg assistant">
              Ask about Trace traffic, DBC decode, or diagnostics. Tools run through openbus Capability
              Bus with policy gates. Write actions require approval.
            </div>
          )}
          {messages.map((m) => (
            <div key={m.id} className={`msg ${m.role}`}>
              {m.text || (m.role === 'assistant' && busy ? '…' : '')}
            </div>
          ))}
          <div ref={bottomRef} />
        </div>

        <div className="composer">
          <textarea
            value={input}
            placeholder="Message the agent…  (e.g. Who is transmitting the most?)"
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter' && !e.shiftKey) {
                e.preventDefault()
                void send()
              }
            }}
            disabled={busy}
          />
          <button type="button" className="btn primary" disabled={busy || !input.trim()} onClick={() => void send()}>
            Send
          </button>
        </div>
      </div>

      <aside className="side">
        <section>
          <h2>Approval</h2>
          {pending ? (
            <div className="approve-card">
              <strong>{pending.tool || 'tool'}</strong>
              <pre>{JSON.stringify(pending.arguments ?? {}, null, 2)}</pre>
              <div className="approve-actions">
                <button type="button" className="btn primary" onClick={() => void resolveApprove(true)}>
                  Approve
                </button>
                <button type="button" className="btn" onClick={() => void resolveApprove(false)}>
                  Reject
                </button>
              </div>
            </div>
          ) : (
            <p style={{ color: 'var(--muted)', margin: 0 }}>No pending write tools</p>
          )}
        </section>

        <section>
          <h2>Settings</h2>
          <div className="field">
            <label>Base URL</label>
            <input
              value={settings.base_url || ''}
              onChange={(e) => setSettings({ ...settings, base_url: e.target.value })}
              onBlur={(e) => void saveSettings({ base_url: e.target.value })}
            />
          </div>
          <div className="field">
            <label>Model</label>
            <input
              value={settings.model || ''}
              onChange={(e) => setSettings({ ...settings, model: e.target.value })}
              onBlur={(e) => void saveSettings({ model: e.target.value })}
            />
          </div>
          <div className="field">
            <label>API Key</label>
            <input
              type="password"
              value={settings.api_key || ''}
              onChange={(e) => setSettings({ ...settings, api_key: e.target.value })}
              onBlur={(e) => void saveSettings({ api_key: e.target.value })}
            />
          </div>
          <div className="field">
            <label>Policy</label>
            <select
              value={settings.policy || 'readonly'}
              onChange={(e) => void saveSettings({ policy: e.target.value })}
            >
              <option value="readonly">readonly</option>
              <option value="tx_allowed">tx_allowed</option>
              <option value="diag_write">diag_write</option>
              <option value="flash">flash</option>
            </select>
          </div>
          <div className="field">
            <label>Role</label>
            <select
              value={settings.role || 'Analyst'}
              onChange={(e) => void saveSettings({ role: e.target.value })}
            >
              <option value="Analyst">Analyst</option>
              <option value="Diagnostics">Diagnostics</option>
              <option value="Developer">Developer</option>
            </select>
          </div>
        </section>

        <section>
          <h2>Activity Snapshot</h2>
          <div className="snap">{snapshot || '(empty)'}</div>
        </section>
      </aside>
    </div>
  )
}
