/**
 * Contract smoke (no Python required): agent-ts build + webui dist + tools sources exist.
 */
import { existsSync, readFileSync } from 'node:fs'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = join(dirname(fileURLToPath(import.meta.url)), '..')
const need = [
  'tools/registry.py',
  'tools/host.py',
  'tools/policy.py',
  'tools/bus_tools.py',
  'webview2_host.py',
  'node_sidecar.py',
  'bridge/server.py',
  'webui/dist/index.html',
  'agent-ts/dist/server.js',
]
let failed = 0
for (const rel of need) {
  const p = join(root, rel)
  const ok = existsSync(p)
  console.log(`${ok ? 'OK' : 'MISS'} ${rel}`)
  if (!ok) failed++
}
const bridge = readFileSync(join(root, 'bridge/server.py'), 'utf8')
for (const token of ['/api/tools', '/api/tools/invoke', '/api/approve', 'X-Vercel-AI-Data-Stream']) {
  const ok = bridge.includes(token)
  console.log(`${ok ? 'OK' : 'MISS'} bridge has ${token}`)
  if (!ok) failed++
}
const index = readFileSync(join(root, 'webui/dist/index.html'), 'utf8')
console.log(`${index.includes('assets/') ? 'OK' : 'MISS'} webui dist hashed assets`)
if (!index.includes('assets/')) failed++

process.exit(failed ? 1 : 0)
