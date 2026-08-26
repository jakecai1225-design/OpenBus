# OAI-02: OpenBUS 命令行 Shell 交互协议设计

## 一、设计目标与愿景

### 1.1 核心使命
**打造"可编程的 CAN 分析引擎"**：
- 🎯 **全功能覆盖**: 支持所有 OpenBUS 现有功能的脚本化调用
- 🔗 **双向通信**: 既能发送命令（控制流），也能接收实时事件（数据流）
- 🧩 **插件友好**: 作为 Python/C++ 外部工具的通用接口
- 🤖 **AI 就绪**: 为 LangGraph Agent 提供确定性的执行环境

### 1.2 对标参考

| 工具 | 特点 | OpenBUS 借鉴点 |
|------|------|----------------|
| **Wireshark CLI (tshark)** | 命令行抓包/导出 | 文件 I/O + 过滤器语法统一 |
| **SQLite3 CLI** | REPL + SQL 语法 | 状态保持 + 查询式数据分析 |
| **LLDB/GDB** | 调试器命令解析 | 结构化命令树 + 错误码体系 |
| **VS Code CLI** | `code --help` 参数化启动 | 子命令模式（subcommand pattern） |

### 1.3 非功能性需求

| 需求类型 | 指标 | 说明 |
|---------|------|------|
| **响应延迟** | <10ms (本地命令) | 避免阻塞用户交互 |
| **吞吐能力** | ≥1000 ops/sec | 批量处理场景 |
| **并发连接** | ≥5 个 TCP 会话 | 多客户端同时监控 |
| **幂等性** | 命令重复执行结果一致 | 便于重试机制 |
| **可审计性** | 每条命令生成日志 | 用于故障复现 |

---

## 二、架构总览

### 2.1 三层架构模型

```
┌─────────────────────────────────────────────────────────┐
│                   User-facing Layer                      │
├───────────────┬───────────────────┬──────────────────────┤
│   CLI REPL    │   TCP Server      │   IPC (Pipe/MemMap) │
│ (interactive) │ (remote control)  │ (cross-process)     │
└───────┬───────┴─────────┬─────────┴──────────┬──────────┘
        │                 │                     │
        └─────────────────┼─────────────────────┘
                          ▼
┌─────────────────────────────────────────────────────────┐
│                Protocol Parsing Layer                    │
├───────────────┬───────────────────┬──────────────────────┤
│   Command Lexer   │  AST Parser    │  Argument Validator  │
│ (token stream)    │ (syntax tree)  │  (schema check)      │
└───────┬───────────┴─────────┬─────┴──────────┬──────────┘
        │                     │                  │
        └─────────────────────┼──────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────┐
│               Execution Engine Layer                     │
├───────────────┬───────────────────┬──────────────────────┤
│   Command Router  │   Plugin Loader  │   Event Dispatcher  │
│ (dispatch table)  │ (dynamically load)| (pub/sub bus)     │
└───────┬───────────┴─────────┬─────┴──────────┬──────────┘
        │                     │                  │
        └─────────────────────┼──────────────────┘
                              ▼
┌─────────────────────────────────────────────────────────┐
│                  OpenBUS Core                            │
├───────────────┬───────────────────┬──────────────────────┤
│ CanTraceModel   │ GraphicView      │ FileIO               │
│ DbcManager      │ PluginManager    │ DeviceManager        │
└──────────────────────────────────────────────────────────┘
```

### 2.2 关键组件说明

#### Component A: Protocol Parsing Layer（协议解析层）
**使命**: 将文本命令转换为结构化执行指令

**输入格式**:  
```python
# 示例 1: 简单命令
trace filter id > 0x100

# 示例 2: 管道式组合
list signals | grep "Engine" | plot y-axis

# 示例 3: 带参数的复杂命令
record start --format blf --output session.blf --dbc my.dbc
```

**处理流程**:
1. **Tokenization**: 按空格/引号分词
2. **AST Construction**: 构建命令树（命令 + 参数 + 子命令）
3. **Validation**: 检查参数类型和必填项
4. **Compilation**: 预解析命令为中间码（提升重复执行性能）

**实现语言**: C++（低延迟优先）或 Rust（内存安全优先）

#### Component B: Command Router（命令路由器）
**使命**: 将命令分发到正确处理器

**路由策略**:
| 命令前缀 | 目标模块 | 示例 |
|---------|---------|------|
| `trace.*` | CanTraceModel | `trace filter`, `trace export` |
| `graphic.*` | GraphicView | `graphic add-signal`, `graphic zoom` |
| `dbc.*` | DbcManager | `dbc list-messages`, `dbc decode` |
| `file.*` | FileIO | `file open`, `file convert` |
| `record.*` | Recorder | `record start`, `record pause` |
| `config.*` | Settings | `config save`, `config reset` |

**扩展机制**:
```cpp
// src/core/shell/command_registry.h
class CommandRegistry {
public:
    void register_command(const std::string& prefix, ICommandHandler* handler);
    
    /// 动态加载（支持热更新）
    void load_plugin(const std::string& path);
};
```

#### Component C: Event Dispatcher（事件总线）
**使命**: 将 OpenBUS 内部事件流推送到 Shell 客户端

**事件分类**:
| 类别 | 事件名 | 触发时机 | 数据结构大小 |
|------|--------|---------|-------------|
| **Trace** | `frame.received` | 新帧到达 | 32 bytes |
| | `trace.filtered` | 过滤命中 | 64 bytes |
| **Graphic** | `signal.added` | 信号添加 | 48 bytes |
| | `cursor.moved` | 卡尺拖动 | 24 bytes |
| **System** | `file.opened` | 文件加载完成 | 128 bytes |
| | `error.detected` | 错误发生 | ≤256 bytes |

**推送模式**:
```python
# 订阅示例
shell subscribe trace.frame.received --filter "can_id > 0x200"
```

---

## 三、协议定义

### 3.1 消息格式（二进制 + JSON）

#### 版本：v1

| 字段 | 长度 | 说明 |
|------|------|------|
| Magic | 2 bytes | `0x5F4F` = "_O" |
| Version | 1 byte | `0x01` |
| Type | 1 byte | 0=Command, 1=Response, 2=Event |
| SequenceID | 4 bytes | 命令 - 响应配对 ID |
| PayloadLen | 4 bytes | JSON payload 长度 |
| Flags | 2 bytes | Bitmask (bit0=压缩，bit1=加密) |
| Payload | N bytes | JSON 字符串 |
| CRC16 | 2 bytes | CRC-16-CCITT 校验 |

#### JSON Payload Schema（Command）
```json
{
  "cmd": "trace.filter",
  "args": {
    "expr": "id > 0x100 && dlc == 8"
  },
  "options": {
    "async": false,
    "timeout_ms": 5000
  }
}
```

#### JSON Payload Schema（Response）
```json
{
  "seq": 123,
  "status": "success",
  "result": {
    "count": 42,
    "frames": [...]
  },
  "latency_us": 1523
}
```

#### JSON Payload Schema（Event）
```json
{
  "event": "frame.received",
  "timestamp_ns": 1722108000123456789,
  "data": {
    "id": 256,
    "dlc": 8,
    "data": "01 02 03...",
    "channel": 1
  }
}
```

### 3.2 传输协议变体

#### 变体 A: Local REPL（标准输入/输出）
```bash
$ openbus --shell
openbus> help
openbus> trace list-signals
```

#### 变体 B: TCP Server（远程控制）
```bash
$ openbus --server --port 5555
# 另一终端
$ nc localhost 5555
```

#### 变体 C: Named Pipe（Windows IPC）
```python
pipe_name = r'\\.\pipe\openbus_shell'
```

#### 变体 D: Unix Domain Socket（Linux/macOS）
```python
socket_path = '/tmp/openbus-control.sock'
```

---

## 四、命令系统详解

### 4.1 命令命名规范

**原则**: `<module>.<action>-<target>`

| 层级 | 示例 | 说明 |
|------|------|------|
| **模块** | `trace` | Trace 表格相关 |
| **动作** | `filter` | 设置过滤条件 |
| **目标** | （无） | 作用于整个模块 |

**边界情况**:
- 单一层次：`help`（顶层命令）
- 两层次：`trace.list` 
- 三层次：`graphic.signal-add`（可选）

### 4.2 核心命令列表

#### 4.2.1 Trace 模块命令集

```bash
# 基础操作
trace list                    # 列出当前所有帧（最多 N 条）
trace count                   # 返回当前帧总数
trace clear                   # 清空所有帧

# 过滤与查询
trace filter set EXPR         # 设置过滤表达式（SQL-like 语法）
trace filter get              # 获取当前过滤条件
trace filter disable          # 临时禁用过滤
trace search PATTERN          # 按 HEX 模式搜索数据域

# 导出与导入
trace export PATH [--format blf|asc|csv]
trace import PATH             # 从文件加载帧数据

# 统计信息
trace stats                   # 显示统计数据（帧数/错误率/DLC 分布）
trace by-id                   # 按 CAN ID 分组统计
trace by-time-window MS       # 窗口内吞吐量计算
```

**示例交互**:
```json
{
  "cmd": "trace.export",
  "args": {"path": "session.csv", "format": "csv"}
}
```

#### 4.2.2 Graphic 模块命令集

```bash
# 信号管理
graphic signal-add NAME [CAN_ID]      # 添加信号到波形区
graphic signal-remove NAME            # 移除指定信号
graphic signal-list                   # 列出当前所有信号
graphic signal-clear ALL              # 清除全部信号

# 视图控制
graphic zoom fit                      # 适应窗口缩放
graphic zoom in / out                 # 放大/缩小
graphic pan x OFFSET                  # 时间轴平移
graphic cursor single                 # 启用单卡尺
graphic cursor double                 # 启用双卡尺
graphic cursor clear                  # 清除卡尺

# 截图与导出
graphic screenshot PATH [--format png/svg]
graphic export-csv SIGNALS... [--path file.csv]
```

#### 4.2.3 DBC 模块命令集

```bash
# 数据库管理
dbc load PATH                         # 加载 DBC 文件
dbc unload                            # 卸载当前 DBC
dbc list                              # 列出所有报文定义
dbc message-list                      # 列出某消息的信号
dbc decode ID DATA...                 # 解码原始数据为信号值

# 信号查询
dbc find-signal NAME-PATTERN          # 模糊查找信号
dbc get-factor SIGNAL_NAME            # 获取因子/偏移
```

#### 4.2.4 File 模块命令集

```bash
# 文件操作
file open PATH                        # 打开 BLF/ASC/CSV 文件
file save-as PATH                     # 另存为
file export SOURCE... DEST [--format] # 格式转换（BLF → CSV）
file recent                           # 最近打开的文件列表

# 元数据
file info                             # 显示文件大小/帧数/创建时间
```

#### 4.2.5 Record 模块命令集

```bash
# 录制控制
record start --output PATH [--format blf]
record pause                          # 暂停录制
record resume                         # 恢复录制
record stop                           # 停止录制
record status                         # 查看当前状态
```

#### 4.2.6 System 模块命令集

```bash
# 会话管理
session new                           # 新建空会话
session load PROJECT_PATH             # 加载工程文件
session save [--path project.json]    # 保存当前配置

# 系统信息
system version                        # 显示 OpenBUS 版本号
system memory                         # 显示内存占用
system log level LEVEL                # 设置日志级别 (debug/info/warn/error)
system disconnect                     # 断开当前 Shell 连接

# 插件管理
plugin list                           # 列出已安装插件
plugin enable NAME                    # 启用插件
plugin disable NAME                   # 禁用插件
```

### 4.3 特殊命令模式

#### 模式 A: 管道链（Pipe Chain）
```bash
trace filter "id > 0x100" | graphic signal-add | record start
```

**实现方式**: 在命令解析阶段构建 AST 的 `pipeline` 节点，各子节点共享一个临时的环形缓冲区

#### 模式 B: 交互式子菜单
```bash
(trace)> help
Available commands:
  filter      # 设置过滤
  export      # 导出数据
  stats       # 统计信息
(trace)> exit
```

#### 模式 C: 宏命令（Macro）
```bash
macro define "quick-diag" <<EOF
trace stats
trace filter "flags contains ERR"
trace export errors.csv
EOF

macro run quick-diag
```

---

## 五、事件系统

### 5.1 事件订阅模型

```python
# 订阅事件
shell subscribe EVENT_NAME [--filter EXPR] [--limit N]

# 取消订阅
shell unsubscribe EVENT_NAME [SUBSCRIPTION_ID]

# 一次性订阅（仅接收第一个匹配的事件）
shell once EVENT_NAME --callback "http://localhost:8080/webhook"
```

#### 示例场景
```json
{
  "cmd": "trace.subscribe",
  "args": {
    "event": "frame.received",
    "filter": "can_id >= 0x200 && can_id <= 0x2FF",
    "limit": 100
  }
}
```

### 5.2 内置事件清单

| 事件名称 | 触发时机 | 负载结构 |
|---------|---------|---------|
| `frame.received` | 实时捕获新帧 | `{id, dlc, data[], channel, timestamp}` |
| `frame.filtered` | 离线回放过滤命中 | 同上 + `{match_reason: "filter_expr"}` |
| `dbc.loaded` | DBC 文件加载完成 | `{message_count, signal_count, filename}` |
| `graphic.signal-added` | 信号添加到图表 | `{signal_name, can_id, color}` |
| `export.completed` | 文件导出结束 | `{path, format, frame_count}` |
| `error.detected` | 运行时错误 | `{code, message, context}` |
| `record.started` / `stopped` | 录制状态变化 | `{path, format, duration}` |

### 5.3 事件推送速率控制

```json
{
  "cmd": "trace.set_event_rate",
  "args": {
    "max_events_per_sec": 1000,
    "batch_size": 100
  }
}
```

**目的**: 防止高频事件冲垮 Shell 客户端

---

## 六、错误码体系

### 6.1 分层错误码

| 类别 | 范围 | 示例 |
|------|------|------|
| **成功** | 0 | `OK` |
| **通用错误** | 1-99 | `ERR_INVALID_ARGS` (4001) |
| **Trace 模块** | 100-199 | `ERR_NO_FRAMES` (1001) |
| **Graphic 模块** | 200-299 | `ERR_SIGNAL_NOT_FOUND` (2001) |
| **DBC 解析** | 300-399 | `ERR_DBC_SYNTAX_ERROR` (3001) |
| **文件 I/O** | 400-499 | `ERR_FILE_NOT_FOUND` (4001) |
| **Shell 协议** | 500-599 | `ERR_MALFORMED_JSON` (5001) |

### 6.2 错误响应格式

```json
{
  "seq": 123,
  "status": "error",
  "code": 4001,
  "message": "File not found: non-existent.asc",
  "details": {
    "path": "non-existent.asc",
    "hint": "Check your file extension (.blf/.asc/.csv supported)"
  },
  "latency_us": 45
}
```

---

## 七、安全性与权限控制

### 7.1 角色模型

| 角色 | 权限范围 | 适用场景 |
|------|---------|---------|
| **Viewer** | 只读命令 | 实时仪表盘监控 |
| **Operator** | 读写命令 | 离线数据分析员 |
| **Admin** | 所有命令 | 系统集成商 |

### 7.2 访问控制列表（ACL）

```json
{
  "user": "viewer",
  "allowed_commands": ["trace.list", "trace.stats", "system.version"],
  "denied_commands": ["record.start", "trace.clear"]
}
```

**实施位置**: Command Router 在执行前校验当前 token 的角色

### 7.3 危险命令确认机制

```bash
# 必须加 `--force` 标志才执行
trace clear --confirm=true
```

或在交互模式中弹出确认对话框（通过 GUI 桥接）。

---

## 八、配置与管理

### 8.1 配置文件

**路径**: `~/.openbus-shells.yaml`（跨平台）

```yaml
# Shell 会话默认配置
default:
  server:
    enabled: true
    port: 5555
    bind_address: "127.0.0.1"
  
  repl:
    history_file: "~/.openbus_history"
    max_history_lines: 1000
  
  security:
    require_auth: false
    default_role: "operator"
  
  event_throttling:
    max_events_per_second: 1000
    batch_size: 50
```

### 8.2 启动选项

```bash
$ openbus --shell [OPTIONS]

Options:
  --server, -s           启动 TCP 服务器模式
  --port PORT            监听端口（默认 5555）
  --repl                 启动本地 REPL 模式
  --pipe NAME            Windows Named Pipe 名称
  --socket PATH          Unix Domain Socket 路径
  --auth TOKEN           认证令牌（若启用 ACL）
  --config FILE          指定配置文件路径
  --verbose, -v          启用调试日志
  --daemon               后台运行（仅 server 模式）
```

---

## 九、测试用例设计

### T1: 基本功能测试

| ID | 命令 | 预期 |
|----|------|------|
| T1-1 | `trace list` | 返回当前帧列表（≤100 条） |
| T1-2 | `trace filter "id > 0x100"` | 过滤生效，下次 list 减少 |
| T1-3 | `graphic signal-add "Speed"` | 信号成功添加，返回色块 |
| T1-4 | `file export session.blf` | 导出文件存在磁盘 |

### T2: 异常处理

| ID | 命令 | 预期 |
|----|------|------|
| T2-1 | `trace filter INVALID_EXPR` | 返回 `ERR_DBC_SYNTAX_ERROR` |
| T2-2 | `file export nonexistent.xyz` | 返回 `ERR_FILE_NOT_FOUND` |
| T2-3 | 连续发送 1000 条 subscribe | 事件被限流，不丢帧 |

### T3: 性能基准

| 场景 | 指标 | 阈值 |
|------|------|------|
| 单条命令平均耗时 | ≤5ms | p99 < 20ms |
| 100 帧批量订阅延迟 | ≤10ms | - |
| 长时间运行内存泄漏 | ≤1% | 连续运行 24h |

---

## 十、实施路线图

### Phase 1: 核心框架（Week 1-2）
- [ ] C++ 命令解析器骨架（ANTLR4 / Lark）
- [ ] 本地 REPL 实现（readline / linenoise）
- [ ] TCP 服务器 stub（asio / boost.asio）
- [ ] 注册表机制（dynamic dispatch）

### Phase 2: Trace 模块命令（Week 3-4）
- [ ] `trace list`, `trace filter`, `trace export`
- [ ] 事件 `frame.received` 推送
- [ ] 错误码体系集成

### Phase 3: Graphic + DBC 命令（Week 5-6）
- [ ] 图形化命令（信号管理/截图）
- [ ] DBC 查询命令（decode/message-list）
- [ ] 跨模块联动（filter → export）

### Phase 4: 安全与控制（Week 7-8）
- [ ] ACL 权限模型
- [ ] 危险命令确认机制
- [ ] 审计日志（记录所有命令）

### Phase 5: 优化与产品化（Week 9-10）
- [ ] 性能压测 + 调优
- [ ] 文档编写（手册页/示例库）
- [ ] Python SDK（方便 AI Agent 调用）

---

## 十一、风险与对策

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| 命令过多导致复杂度爆炸 | 高 | 维护成本 ↑ | 模块化 + 自动文档生成（`--help --json`） |
| TCP 连接崩溃导致数据丢失 | 中 | 可靠性 ↓ | 心跳检测 + 断线重连 + 序列号 |
| 高频事件冲垮主线程 | 中 | UI 卡顿 | 异步事件队列 + 背压机制 |
| AI 误发破坏性命令 | 低 | 用户投诉 | ACL 角色限制 + 二次确认 |

---

---

## 十三、AI Agent 集成方案

### 13.1 整体架构

```
┌─────────────────────────────────────────────────────────┐
│          LangGraph AI Agent (Python)                     │
├───────────────┬───────────────────┬──────────────────────┤
│   Intent        │   Tool Executor    │   Memory Store       │
│   Classifier    │                    │  (Session Context)   │
└───────┬─────────┴─────────┬─────────┴──────────┬──────────┘
        │                   │                     │
        └───────────────────┼─────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────┐
│              OpenBUS Shell Bridge                        │
├───────────────────┬───────────────────────┬──────────────┤
│   Command Sender  │   Event Subscriber    │   Response   │
│   (TCP Client)    │   (Async Event Loop)  │   Parser     │
└──────────┬────────┴───────────┬───────────┴───────┬──────┘
           │                     │                   │
           └─────────────────────┼───────────────────┘
                                 ▼
┌─────────────────────────────────────────────────────────┐
│               OpenBUS Core Process                       │
├───────────────────┬───────────────────────┬──────────────┤
│ CanTraceModel     │ GraphicView           │ DbcManager   │
└───────────────────┴───────────────────────┴──────────────┘
```

### 13.2 通信链路详解

#### 组件 A: Command Sender（命令发送器）
**使命**: 将 AI Agent 生成的工具调用转换为 Shell 命令

**输入格式 **(LangGraph Tool Call):
```python
{ "name": "trace_filter", "arguments": { "expr": "id > 0x100" } }
```

**转换规则**:
```python
def tool_call_to_shell(tool_call: dict) -> str:
    """将 Tool 调用转为 Shell DSL"""
    if tool_call["name"] == "trace_filter":
        return f"trace filter set {tool_call['arguments']['expr']}"
    elif tool_call["name"] == "add_signal":
        sig = tool_call["arguments"]["signal_name"]
        can_id = tool_call["arguments"]["can_id"]
        return f"graphic signal-add {sig} {can_id}"
    # ... 所有工具的映射表
    raise NotImplementedError(f"Tool {tool_call['name']} not supported")
```

**异步发送模式**:
```python
async def send_command(shell_client: ShellClient, command: str):
    seq_id = generate_uuid()
    payload = {
        "cmd": command,
        "options": {"async": True, "timeout_ms": 5000},
        "__meta": {"source": "ai_agent", "session_id": current_session}
    }
    await shell_client.send_binary(payload)
    return seq_id
```

#### 组件 B: Event Subscriber（事件订阅器）
**使命**: 监听 OpenBUS 事件流并回传给 AI 上下文

**订阅策略**:
```python
# 自动订阅相关事件
AUTO_SUBSCRIBE_EVENTS = {
    "trace.filter": ["frame.filtered"],
    "graphic.add-signal": ["graphic.signal-added", "error.detected"],
    "file.export": ["export.completed", "error.detected"]
}

async def setup_ai_subscriptions(agent_context):
    for action, events in AUTO_SUBSCRIBE_EVENTS.items():
        for event_name in events:
            await shell_client.subscribe(
                event_name,
                filter=f"source == 'ai_agent' AND session == '{agent_context.id}'",
                callback=lambda ev: ai_context.update(ev)
            )
```

**事件流处理**:
```python
@shell_client.on_event("frame.filtered")
async def on_filtered_frame(event_data: dict):
    """将过滤结果注入 AI 记忆"""
    agent_context.memory.append({
        "type": "filter_result",
        "count": len(event_data["frames"]),
        "sample": event_data["frames"][:10]  # 限制样本大小
    })
```

#### 组件 C: Response Parser（响应解析器）
**使命**: 将 Shell 响应结构化后返回给 AI

**成功响应处理**:
```json
{
  "seq": 123,
  "status": "success",
  "result": { "count": 42 },
  "latency_us": 1523
}
```

```python
def parse_shell_response(response: dict) -> AIMessage:
    return AIMessage(
        role="tool_response",
        name=response.get("cmd"),
        content=json.dumps(response["result"], indent=2),
        meta={"latency": response["latency_us"], "error_code": None}
    )
```

**错误响应处理**:
```json
{
  "seq": 124,
  "status": "error",
  "code": 4001,
  "message": "Invalid filter expression",
  "details": { "hint": "Check your syntax near position 15" }
}
```

```python
if response["status"] == "error":
    # 构造可恢复的错误对象供 AI 重试
    return AIMessage.error(
        tool_name=response["cmd"],
        error_code=response["code"],
        suggestion=response["details"].get("hint")
    )
```

### 13.3 LangGraph 节点集成示例

```python
from langgraph.graph import StateGraph
from typing import TypedDict

class AIAgentState(TypedDict):
    user_query: str
    shell_commands: list[str]
    shell_results: list[dict]
    ai_decision: str

# 节点 1: 意图分析 → 生成计划
def plan_shell_actions(state: AIAgentState) -> AIAgentState:
    """LLM 分解用户查询为 Shell 命令序列"""
    prompt = f"""
    你是一个 CAN 总线分析助手。根据用户需求生成 Shell 命令序列。
    
    可用命令参考：<TOOL_DESCRIPTIONS>
    
    用户需求：{state['user_query']}
    
    返回 JSON 数组:
    [
      {{"cmd": "trace.filter", "args": {{"expr": "id > 0x100"}}}},
      {{"cmd": "graphic.signal-add", "args": {{"signal": "Speed"}}}}
    ]
    """
    llm_response = llm.invoke(prompt)
    commands = parse_json_array(llm_response.content)
    state["shell_commands"] = [tool_call_to_shell(c) for c in commands]
    return state

# 节点 2: 执行命令序列
def execute_shell_pipeline(state: AIAgentState) -> AIAgentState:
    """串行执行 Shell 命令，收集结果"""
    results = []
    for cmd in state["shell_commands"]:
        seq_id = await send_command(cmd)
        # 等待响应（含超时）
        response = await shell_client.wait_response(seq_id, timeout=5000)
        
        if response["status"] == "error":
            # 触发恢复机制
            return {
                **state,
                "ai_decision": f"ERROR_IN_{response['cmd']}_NEED_RETRY"
            }
        
        results.append(response)
    
    state["shell_results"] = results
    return state

# 节点 3: 决策树 - 根据结果决定下一步
def decide_next_action(state: AIAgentState) -> AIAgentState:
    if state.get("ai_decision") == "ERROR_IN_TRACE_FILTER":
        # 自动尝试简化表达式
        state["shell_commands"].append("trace filter disable")
        return {"flow": "execute"}
    elif len(state["shell_results"]) == 0:
        return {"flow": "ask_user_clarification"}
    else:
        return {"flow": "generate_human_readable_report"}

# 组装图
workflow = StateGraph(AIAgentState)
workflow.add_node("plan", plan_shell_actions)
workflow.add_node("execute", execute_shell_pipeline)
workflow.add_node("decide", decide_next_action)

workflow.set_entry_point("plan")
workflow.add_conditional_edges(
    "plan",
    lambda s: "execute" if s["shell_commands"] else "ask_user_clarification"
)
workflow.add_edge("execute", "decide")
workflow.add_edge("decide", END)

app = workflow.compile()
```

### 13.4 AI 就绪的命令规范

为确保 AI Agent 能稳定可靠地调用，以下命令需要特别优化：

#### 优化 1: 确定性输出
```bash
# ❌ 坏：响应格式随环境变化
trace stats
# 输出：Found 123 frames (Error rate: 0.8%)

# ✅ 好：结构化输出
trace stats --format json
# 输出：{"frame_count": 123, "error_rate_percent": 0.8, "dlc_distribution": {...}}
```

#### 优化 2: 幂等性保障
```bash
# 重复执行结果一致
graphic signal-add "Speed" 0x1D6  # 第一次：新增
graphic signal-add "Speed" 0x1D6  # 第二次：忽略（已存在）→ 静默成功
```

#### 优化 3: 分步确认机制
```json
{
  "cmd": "record.start",
  "args": {"output": "session.blf"},
  "confirmation_required": true,
  "confirm_prompt": "确定开始录制？这将覆盖之前录制的文件。\n[y/n]"
}
```

#### 优化 4: 超时与重试策略
```python
def with_retry_decorator(max_retries=3):
    def decorator(func):
        @wraps(func)
        async def wrapper(*args, **kwargs):
            for i in range(max_retries):
                try:
                    return await func(*args, **kwargs)
                except TimeoutError:
                    if i == max_retries - 1:
                        raise
                    await asyncio.sleep(2 ** i)  # 指数退避
            return None
        return wrapper
    return decorator

@with_retry_decorator(max_retries=3)
async def trace_filter(expr: str):
    return await shell_client.execute(f"trace filter set {expr}")
```

### 13.5 Python SDK for AI Agent

提供简化的 Python 客户端封装：

```python
from openbus_shell import ShellClient

class OpenBUSAICore:
    def __init__(self, server_url="127.0.0.1:5555"):
        self.shell = ShellClient(server_url)
        self.memory = AIMemoryStore()
    
    async def analyze(self, query: str) -> str:
        """主入口：用户查询 → AI 分析 → 自然语言报告"""
        # 1. 生成命令计划
        commands = self.llm_plan(query)
        
        # 2. 执行并捕获结果
        results = []
        for cmd in commands:
            resp = await self.shell.execute(cmd)
            results.append(resp)
            if resp.status == "error":
                # 尝试修复策略
                cmd = self.auto_retry(cmd, resp.error)
        
        # 3. 汇总为人类可读报告
        report = self.generate_report(results)
        return report
    
    async def subscribe_events(self, filter_expr: str):
        """订阅实时帧数据到 AI 记忆"""
        await self.shell.subscribe(
            "frame.received",
            filter=filter_expr,
            callback=self.memory.append
        )

# 使用示例
async def main():
    ai = OpenBUSAICore()
    result = await ai.analyze(
        "帮我分析一下刚才采集的数据里有哪些异常帧"
    )
    print(result)
```

### 13.6 安全隔离机制

#### 13.6.1 权限隔离
```yaml
# ~/.openbus-shells.yaml
ai_agent:
  role: "operator"  # 比 viewer 高一级，但不能删除工程
  allowed_tools: 
    - "trace.*"     # 允许 Trace 读取
    - "graphic.*"   # 允许可视化操作
  denied_tools:
    - "trace.clear" # 禁止清空数据
    - "session.save" # 禁止保存工程
```

#### 13.6.2 危险操作沙箱
```python
# 模拟执行而非直接执行
async def safe_execute(cmd: str, dry_run: bool = True):
    """Dry Run 模式下仅验证合法性"""
    validation_resp = await shell_client.validate(cmd)
    if not validation_resp.valid:
        raise PermissionError(validation_resp.message)
    
    if dry_run:
        return SimulationResult(estimates={"duration_ms": 150})
    
    return await shell_client.execute(cmd)
```

### 13.7 调试与观测

#### 13.7.1 命令追踪 ID
```python
import uuid

class TracedCommand:
    id = str(uuid.uuid4())
    timestamp_ns = time.time_ns()
    source = "ai_agent"
    parent_session = current_session_id

# 每条命令附带元数据
{"cmd": "trace.filter", "__trace_id": "abc123", "__meta": {...}}
```

#### 13.7.2 实时监控面板
```python
async def monitor_ai_shell_metrics():
    while True:
        latency = await shell_client.get_avg_latency()
        queue_size = await shell_client.get_pending_commands()
        error_rate = await shell_client.get_error_rate()
        
        print(f"AI Shell Metrics:")
        print(f"  Avg Latency: {latency:.2f}ms")
        print(f"  Pending Commands: {queue_size}")
        print(f"  Error Rate: {error_rate*100:.2f}%")
        
        await asyncio.sleep(1)
```

---

## 十四、总结与展望

### D1: 命令语言选型
- [ ] **方案 A**: 自定义 DSL（灵活但需学习成本）
- [ ] **方案 B**: SQL 简化版（已有生态但语法冗长）

**建议**: 初期用简化 DSL，后期兼容 SQL 子集

### D2: 传输协议优先级
- [ ] 方案 A: TCP/IP（跨平台 + 防火墙友好）
- [ ] 方案 B: Named Pipe（仅限 Windows）

**建议**: 以 TCP 为主，额外提供 Named Pipe 作为 Windows 特供

### D3: 安全性深度
- [ ] 方案 A: 仅本地 Socket（无网络暴露）
- [ ] 方案 B: 支持 TLS 加密远程连接

**建议**: V1 仅本地，V2 增加 TLS 开关

---

## 附录：参考资源

1. [SQLite3 Command Language](https://www.sqlite.org/lang.html)
2. [Wireshark tshark CLI](https://wiki.wireshark.org/TShark)
3. [gRPC Protobuf API Design](https://grpc.io/docs/guides/concepts/)
4. [Chrome DevTools Protocol](https://chromedevtools.github.io/devtools-protocol/)
5. [ANTLR4 Grammar Definition](https://www.antlr.org/)
