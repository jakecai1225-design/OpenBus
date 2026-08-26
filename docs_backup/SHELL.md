# OpenBUS CLI Shell — Unified Specification v3.0

## 📋 Overview

OpenBUS CLI Shell provides a programmatic interface to control all core functionality via JSON-RPC 2.0 over TCP. This enables AI agents, external automation tools, and scripting languages to interact with the CAN/CAN FD analysis platform without UI dependencies.

---

## 🎯 Design Philosophy

1. **Zero New Dependencies**: Leverage existing Qt6 Network + nlohmann/json (already in use)
2. **Standard Protocol**: JSON-RPC 2.0 for tool compatibility (curl/Postman/any language)
3. **Complete Coverage**: Expose ALL core OpenBUS capabilities via CLI
4. **Clean Architecture**: Minimal coupling, reusable command registry pattern
5. **Production Ready**: Stable MinGW + Qt6.8 compatibility (no AUTOMOC issues)

---

## 🔧 Technology Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
| **Transport** | QTcpServer | Qt6 native, stable, no build complications |
| **Protocol** | JSON-RPC 2.0 | Standardized, documented, tool-friendly |
| **JSON** | nlohmann/json | Already integrated, header-only |
| **Port** | 5555 (default) | Configurable via `--shell-port` |
| **Build** | CMake + Ninja | Zero new dependencies, reuse existing toolchain |

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────┐
│         OpenBUS Core Application            │
├─────────────────────────────────────────────┤
│  ┌─────────────────────────────────────┐   │
│  │  TcpRpcServer (port 5555)           │   │
│  │  - QTcpServer backend               │   │
│  │  - Connection management            │   │
│  │  - Message framing                  │   │
│  └──────────────┬──────────────────────┘   │
│                 │                           │
│  ┌──────────────▼──────────────────────┐   │
│  │  JsonRpcHandler                     │   │
│  │  - Method routing                   │   │
│  │  - Parameter validation             │   │
│  │  - Error handling                   │   │
│  └──────────────┬──────────────────────┘   │
│                 │                           │
│  ┌──────────────▼──────────────────────┐   │
│  │  Command Registry                   │   │
│  │  - core.* commands                  │   │
│  │  - player.* commands                │   │
│  │  - recorder.* commands              │   │
│  │  - simulator.* commands             │   │
│  │  - trace.* commands                 │   │
│  │  - dbc.* commands                   │   │
│  │  - project.* commands               │   │
│  │  - config.* commands                │   │
│  │  - device.* commands                │   │
│  │  - flow.* commands                  │   │
│  └─────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
```

### Key Design Decisions

- **No HTTP Overhead**: Raw TCP for minimal latency (not Boost.Beast or Qt HTTP Server)
- **Command Pattern**: Each CLI command maps to a function that wraps existing core APIs
- **State Isolation**: Commands operate on singleton managers (AppConfig, DbcManager, Player, etc.)
- **Graceful Shutdown**: Server stops cleanly when application exits

---

## 📦 Complete Command Reference

### Category: Core (`core.*`)

#### `core.load` - Load data file (DBC/ASC/BLF/etc.)
```json
{
  "jsonrpc": "2.0",
  "method": "core.load",
  "params": {
    "path": "/path/to/file.dbc",
    "type": "dbc"
  },
  "id": 1
}
```
**Response:**
```json
{"jsonrpc":"2.0","result":{"success":true,"message":"Loaded DBC: 123 signals"},"id":1}
```

**Parameters:**
- `path` (string): Absolute path to file
- `type` (string): File type (`dbc`, `asc`, `blf`, `csv`, `pcap`, `trc`)

**Error Codes:**
- `INVALID_PATH`: File not found
- `UNSUPPORTED_FORMAT`: Unknown file type
- `PARSE_ERROR`: File format invalid

---

### Category: Player (`player.*`)

#### `player.start` - Start playback of loaded trace data
```json
{
  "jsonrpc": "2.0",
  "method": "player.start",
  "params": {
    "speed": 1.0,
    "loop": false
  },
  "id": 2
}
```

**Parameters:**
- `speed` (float): Playback speed multiplier (default: 1.0)
- `loop` (bool): Whether to loop (default: false)

#### `player.stop` - Stop playback
```json
{
  "jsonrpc": "2.0",
  "method": "player.stop",
  "params": {},
  "id": 3
}
```

#### `player.pause` - Pause playback
```json
{
  "jsonrpc": "2.0",
  "method": "player.pause",
  "params": {},
  "id": 4
}
```

#### `player.resume` - Resume from pause
```json
{
  "jsonrpc": "2.0",
  "method": "player.resume",
  "params": {},
  "id": 5
}
```

#### `player.status` - Get current playback status
```json
{
  "jsonrpc": "2.0",
  "method": "player.status",
  "params": {},
  "id": 6
}
```
**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "running",
    "position": 12345,
    "speed": 1.0,
    "loop": false
  },
  "id": 6
}
```

---

### Category: Recorder (`recorder.*`)

#### `recorder.start` - Start recording to file
```json
{
  "jsonrpc": "2.0",
  "method": "recorder.start",
  "params": {
    "path": "/path/to/recording.blf",
    "format": "blf"
  },
  "id": 7
}
```

**Parameters:**
- `path` (string): Output file path
- `format` (string): Output format (`blf`, `asc`, `csv`)

#### `recorder.stop` - Stop recording
```json
{
  "jsonrpc": "2.0",
  "method": "recorder.stop",
  "params": {},
  "id": 8
}
```

#### `recorder.status` - Check if recording active
```json
{
  "jsonrpc": "2.0",
  "method": "recorder.status",
  "params": {},
  "id": 9
}
```

---

### Category: Simulator (`simulator.*`)

#### `simulator.config` - Configure simulation parameters
```json
{
  "jsonrpc": "2.0",
  "method": "simulator.config",
  "params": {
    "enabled": true,
    "interval_ms": 100,
    "frame_id": 123,
    "payload": "abcd1234"
  },
  "id": 10
}
```

#### `simulator.start` - Start generating fake frames
```json
{
  "jsonrpc": "2.0",
  "method": "simulator.start",
  "params": {},
  "id": 11
}
```

#### `simulator.stop` - Stop simulation
```json
{
  "jsonrpc": "2.0",
  "method": "simulator.stop",
  "params": {},
  "id": 12
}
```

#### `simulator.toggle` - Toggle simulation on/off
```json
{
  "jsonrpc": "2.0",
  "method": "simulator.toggle",
  "params": {},
  "id": 13
}
```

---

### Category: Trace (`trace.*`)

#### `trace.list` - List all loaded CAN frames
```json
{
  "jsonrpc": "2.0",
  "method": "trace.list",
  "params": {
    "limit": 100,
    "offset": 0
  },
  "id": 14
}
```
**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "total": 12345,
    "frames": [
      {"timestamp": 1234567890, "id": 123, "dlc": 8, "data": "abcd1234"},
      ...
    ]
  },
  "id": 14
}
```

**Parameters:**
- `limit` (int): Max frames to return (default: 100)
- `offset` (int): Starting offset (default: 0)

#### `trace.filter` - Apply frame ID filter
```json
{
  "jsonrpc": "2.0",
  "method": "trace.filter",
  "params": {
    "frame_ids": [123, 456, 789],
    "include_can_fd": true
  },
  "id": 15
}
```

#### `trace.clear` - Clear all buffered frames
```json
{
  "jsonrpc": "2.0",
  "method": "trace.clear",
  "params": {},
  "id": 16
}
```

#### `trace.export` - Export frames to file
```json
{
  "jsonrpc": "2.0",
  "method": "trace.export",
  "params": {
    "path": "/path/to/export.asc",
    "format": "asc",
    "filter": {"frame_ids": [123]}
  },
  "id": 17
}
```

#### `trace.stats` - Get trace statistics
```json
{
  "jsonrpc": "2.0",
  "method": "trace.stats",
  "params": {},
  "id": 18
}
```
**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "total_frames": 12345,
    "can_frames": 10000,
    "can_fd_frames": 2345,
    "unique_ids": 150,
    "time_range": {"start": 1234567890, "end": 1234567990}
  },
  "id": 18
}
```

---

### Category: DBC (`dbc.*`)

#### `dbc.list` - List all loaded DBC files
```json
{
  "jsonrpc": "2.0",
  "method": "dbc.list",
  "params": {},
  "id": 19
}
```

#### `dbc.signal.query` - Query signal definition
```json
{
  "jsonrpc": "2.0",
  "method": "dbc.signal.query",
  "params": {
    "message": "EngineStatus",
    "signal": "RPM"
  },
  "id": 20
}
```
**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": {
    "name": "RPM",
    "start_bit": 0,
    "length": 16,
    "endian": "little",
    "scale": 0.1,
    "offset": 0,
    "min": 0,
    "max": 9999.9
  },
  "id": 20
}
```

#### `dbc.message.query` - Query message definition
```json
{
  "jsonrpc": "2.0",
  "method": "dbc.message.query",
  "params": {
    "message": "EngineStatus"
  },
  "id": 21
}
```

#### `dbc.parse` - Parse raw DBC content
```json
{
  "jsonrpc": "2.0",
  "method": "dbc.parse",
  "params": {
    "content": "VERSION \"...\"\nNS_ ...\nBS_...\n"
  },
  "id": 22
}
```

---

### Category: Project (`project.*`)

#### `project.new` - Create new empty project
```json
{
  "jsonrpc": "2.0",
  "method": "project.new",
  "params": {
    "name": "MyProject"
  },
  "id": 23
}
```

#### `project.save` - Save current project
```json
{
  "jsonrpc": "2.0",
  "method": "project.save",
  "params": {
    "path": "/path/to/project.openbusproj"
  },
  "id": 24
}
```

#### `project.load` - Load project from file
```json
{
  "jsonrpc": "2.0",
  "method": "project.load",
  "params": {
    "path": "/path/to/project.openbusproj"
  },
  "id": 25
}
```

#### `project.info` - Get current project info
```json
{
  "jsonrpc": "2.0",
  "method": "project.info",
  "params": {},
  "id": 26
}
```

---

### Category: Config (`config.*`)

#### `config.get` - Get configuration value
```json
{
  "jsonrpc": "2.0",
  "method": "config.get",
  "params": {
    "key": "theme"
  },
  "id": 27
}
```

#### `config.set` - Set configuration value
```json
{
  "jsonrpc": "2.0",
  "method": "config.set",
  "params": {
    "key": "theme",
    "value": "Dark"
  },
  "id": 28
}
```

#### `config.list` - List all config keys
```json
{
  "jsonrpc": "2.0",
  "method": "config.list",
  "params": {},
  "id": 29
}
```

---

### Category: Device (`device.*`)

#### `device.enumerate` - List available hardware devices
```json
{
  "jsonrpc": "2.0",
  "method": "device.enumerate",
  "params": {
    "vendor": "zlg"
  },
  "id": 30
}
```
**Response:**
```json
{
  "jsonrpc": "2.0",
  "result": [
    {"handle": "USB123", "vendor": "zlg", "product": "ZCAN Pro", "serial": "ABC123"},
    ...
  ],
  "id": 30
}
```

#### `device.connect` - Connect to hardware device
```json
{
  "jsonrpc": "2.0",
  "method": "device.connect",
  "params": {
    "handle": "USB123",
    "bitrate": 500000,
    "fd": true,
    "data_bitrate": 2000000
  },
  "id": 31
}
```

#### `device.disconnect` - Disconnect device
```json
{
  "jsonrpc": "2.0",
  "method": "device.disconnect",
  "params": {},
  "id": 32
}
```

#### `device.send` - Send raw CAN frame via hardware
```json
{
  "jsonrpc": "2.0",
  "method": "device.send",
  "params": {
    "id": 123,
    "data": "abcd1234",
    "dlc": 8,
    "fd": false
  },
  "id": 33
}
```

---

### Category: Flow (`flow.*`)

#### `flow.list` - List all measurement flows
```json
{
  "jsonrpc": "2.0",
  "method": "flow.list",
  "params": {},
  "id": 34
}
```

#### `flow.create` - Create new flow
```json
{
  "jsonrpc": "2.0",
  "method": "flow.create",
  "params": {
    "name": "TestFlow",
    "modules": ["trace", "graphic"]
  },
  "id": 35
}
```

#### `flow.configure` - Configure flow parameters
```json
{
  "jsonrpc": "2.0",
  "method": "flow.configure",
  "params": {
    "flow_id": "abc123",
    "settings": {...}
  },
  "id": 36
}
```

#### `flow.start` - Start flow execution
```json
{
  "jsonrpc": "2.0",
  "method": "flow.start",
  "params": {
    "flow_id": "abc123"
  },
  "id": 37
}
```

#### `flow.stop` - Stop flow execution
```json
{
  "jsonrpc": "2.0",
  "method": "flow.stop",
  "params": {
    "flow_id": "abc123"
  },
  "id": 38
}
```

---

## 🛠️ Implementation Phases

### Phase 1: Foundation (Day 1)
- [x] Architecture design finalized
- [ ] Command registry implementation
- [ ] Basic command handlers (core.load, player.start, trace.list)
- [ ] Unit tests for command parsing

### Phase 2: Core Features (Day 2-3)
- [ ] Player commands (start/stop/pause/status)
- [ ] Recorder commands (start/stop/status)
- [ ] Trace commands (list/filter/clear/stats/export)
- [ ] DBC commands (list/query)
- [ ] Integration tests

### Phase 3: Advanced Features (Day 4-5)
- [ ] Simulator commands
- [ ] Device commands (enumerate/connect/send)
- [ ] Project commands (save/load/new)
- [ ] Config commands (get/set/list)
- [ ] Flow commands (CRUD operations)

### Phase 4: Polish & Testing (Day 6-7)
- [ ] Error handling & validation
- [ ] Security (command injection prevention)
- [ ] Performance optimization
- [ ] Documentation (Python SDK examples)
- [ ] Stress testing (concurrent connections)

---

## 📁 File Structure

```
src/core/shell/
├── http_rpc_server.h          # TCP server + JSON-RPC handler (existing)
├── http_rpc_server.cpp        # Server implementation (existing)
├── command_registry.h         # NEW: Command registration API
├── command_registry.cpp       # NEW: All command implementations
├── cmd_core.cpp               # NEW: core.* commands
├── cmd_player.cpp             # NEW: player.* commands
├── cmd_recorder.cpp           # NEW: recorder.* commands
├── cmd_simulator.cpp          # NEW: simulator.* commands
├── cmd_trace.cpp              # NEW: trace.* commands
├── cmd_dbc.cpp                # NEW: dbc.* commands
├── cmd_project.cpp            # NEW: project.* commands
├── cmd_config.cpp             # NEW: config.* commands
├── cmd_device.cpp             # NEW: device.* commands
└── cmd_flow.cpp               # NEW: flow.* commands

doc/
└── SHELL.md                   # UNIFIED specification (replaces old docs)
```

---

## ✅ Acceptance Criteria

1. **Build Success**: `python scripts/build.py build` compiles without errors
2. **Runtime Startup**: Application starts shell server on port 5555 when `--enable-shell` flag used
3. **Command Execution**: `curl -X POST http://localhost:5555 -d '{"jsonrpc":"2.0","method":"trace.list","params":{},"id":1}'` returns valid response
4. **All Commands Working**: 38+ commands implemented and tested
5. **Error Handling**: Invalid commands return proper JSON-RPC error objects
6. **Stress Test**: 50 concurrent connections, p99 latency < 100ms
7. **Documentation**: Python SDK example script works end-to-end

---

## 🐍 Python SDK Example

```python
import socket
import json

class ShellClient:
    def __init__(self, host='localhost', port=5555):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((host, port))
    
    def call(self, method, params=None):
        request = {
            'jsonrpc': '2.0',
            'method': method,
            'params': params or {},
            'id': 1
        }
        self.sock.sendall(json.dumps(request).encode())
        response = self.sock.recv(4096)
        return json.loads(response.decode())['result']

# Usage
client = ShellClient()
stats = client.call('trace.stats')
print(f"Total frames: {stats['total_frames']}")
```

---

## 🔗 References

- **JSON-RPC 2.0 Spec**: https://www.jsonrpc.org/specification
- **Qt TcpServer Docs**: https://doc.qt.io/qt-6/qtcpservers.html
- **Existing Core APIs**: Refer to `src/core/` documentation
- **Previous Drafts**: 
  - CLI-SHELL-FINAL-Implementation.md (v2.1 - HTTP-based approach)
  - CLI-SHELL-New-Design-V2.md (v2.0 - Boost.Beast approach)

---

## ⚠️ Breaking Changes vs Old Specs

1. **TCP Instead of HTTP**: Removed HTTP overhead, simplified protocol
2. **No Boost Dependency**: Avoided MinGW build complications
3. **Expanded Command Set**: Added 38+ commands (vs limited set in old docs)
4. **Unified Doc**: Merged 2 conflicting specs into single authoritative version

---

## 🎯 Success Metrics

- **Development Velocity**: Zero AUTOMOC build errors
- **Command Coverage**: 100% of core OpenBUS functions accessible via CLI
- **Performance**: Sub-millisecond latency for simple queries
- **Reliability**: No crashes under concurrent load

---

## 📝 Revision History

- **v3.0** (2026-08-25): Unified spec, expanded commands, TCP transport
- **v2.1** (2026-08-24): HTTP/RPC hybrid approach (abandoned)
- **v2.0** (2026-08-23): Boost.Beast proposal (abandoned)
- **v1.0** (2026-08-22): Initial concept (custom binary protocol, abandoned)
