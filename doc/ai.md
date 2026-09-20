# openbus AI-Native 平台方案

> 文档：`doc/ai.md`  
> 定位：全生命周期、全场景的 **AI-Native 产品架构**（平台层）  
> 配套：Agent 插件实现细节见 [`aiagent.md`](aiagent.md)；插件传输契约见 [`插件方案.md`](插件方案.md)；领域套件策略见 [`Plugin_Domain_Suites.md`](Plugin_Domain_Suites.md)  
> 现状锚点：`plugins/ai-agent` v0.2.0（自研 ReAct tool-loop + Policy/HITL + `sin.*` 读/有限写）  
> 原则：**不重复造轮子** — 编排层采用业界成熟开源 Agent 框架；openbus 只做总线领域能力、策略与「一切可投喂」上下文。  
> 日期：2026-09-20（增补：Context Attach / 多 Agent 选型）

---

## 0. 一句话目标

把 openbus 从「带 AI 聊天窗的总线 IDE」升级为 **AI-Native 总线工作台**：

- **AI 能用全软件**：主进程能力 + 全部领域套件，都以可发现、可鉴权、可审计的工具/资源暴露给 Agent。  
- **AI 能赋能全软件**：每个套件、Trace、市场、工程生命周期都有「Ask / Add to Chat / Explain / Fix / Generate」入口。  
- **一切可投喂**：文件、目录、Log、Trace 行、DBC 信号、EDS 对象、OUTPUT 行等，均可右键 **Add to AI Chat**，变成可引用、可撤销的上下文芯片（对标 Cursor `@` / Add to Chat）。  
- **编排不自研**：任务分解与多智能体协作采用成熟开源框架；我们只接 Capability Bus、Policy 与领域专家 Agent。  
- **隔离但不割裂**：AI 与领域插件逻辑隔离、权限隔离、失败隔离；通过统一 **Capability Bus** 协作。

---

## 1. 问题陈述（今天为什么不够）

### 1.1 已有资产

| 层 | 现状 |
|----|------|
| 主进程 | C++/Qt：Trace、设备、DBCManager、PluginManager、ZMQ Hub |
| 插件宿主 | 单一 `sin_host.py` 进程，多插件 **同进程加载** |
| SDK | `sin.frames` / `dbc` / `signals` / `workspace` / `commands` / `files` / `output` / `ui` |
| 领域套件 | UDS / DBC / CANopen / J1939 / OBD / AUTOSAR / EtherCAT / … |
| AI Agent | 独立领域插件：Orchestrator + ToolRegistry + Policy + ChatWindow |

### 1.2 结构性缺口

1. **能力面碎片化** — Agent 硬编码少量 tool；套件不发布工具清单；UDS/OBD「facade」多数是自拼 CAN 帧，未真正驱动套件会话与 ISO-TP 等待。  
2. **宿主 RPC 不全** — 缺 `device.getStatus`、`plugins.list` / `plugins.activate`、深度 Trace 查询等，Agent 只能「猜」总线与插件状态。  
3. **无跨插件契约** — 只有 `sin.commands.execute("udsSuite.open")` 与 `state_store` 软跳转；没有 Tool/Resource 注册表。  
4. **隔离偏软** — 主进程 ↔ Python 宿主有进程边界；插件之间 **无** OS 沙箱，恶意/崩溃插件可影响同宿主全部套件与总线发送。  
5. **赋能单向** — AI 窗口可调工具，但 Trace/套件页面几乎没有内嵌 AI 入口；**无** Cursor 式「Add to Chat / @引用」；生命周期未产品化。  
6. **编排自研天花板** — 当前自研 `Orchestrator` 够演示单 Agent tool-loop，缺任务分解、子 Agent handoff、持久化 HITL 图；不应继续堆轮子。  
7. **文档与实现漂移** — 市场文案仍写「只读」，Phase 2 已有 HITL 写路径；`aiagent.md` Phase 3（MCP）尚未落地。

---

## 2. 产品愿景：三个「全」

### 2.1 全生命周期（Lifecycle）

| 阶段 | AI 角色 | 典型动作 |
|------|---------|----------|
| **安装 / 入门** | Onboarding Agent | 解释通道连接、推荐套件、生成「第一次 Trace」检查清单 |
| **工程创建** | Workspace Agent | 打开/关联 DBC·EDS·ARXML·ESI；校验工程完整性 |
| **分析** | Analyst | Trace 顶谈者、异常周期、信号解释、DBC 冲突 |
| **诊断** | Diagnostics | UDS/OBD/J1939 DM；DID 读、DTC 解释；默认只读 |
| **测试 / 台架** | TestEngineer | 生成 TX 周期表、UDS 序列、压力场景；HITL 后执行 |
| **安全** | SafetyOfficer | 拒绝刷写/爆破；审计导出；策略门控说明 |
| **交付** | Reporter | 会话 → Markdown/PDF 报告；可复现 tool 轨迹 |
| **迭代** | Eval / Replay | 黄金用例回归 Agent 行为；策略变更对比 |

### 2.2 全应用场景（Scenarios）

- **交互式**：Chat、**Add to AI Chat**、套件内 Ask、Trace 选中帧 Explain。  
- **后台**：测量运行中的异常检测订阅（频率突增、Bus-Off 征兆）。  
- **批处理**：无头脚本 / CI：MCP 或 headless agent 跑只读分析。  
- **跨工具**：外部 Cursor / Claude Desktop 通过 MCP 复用同一 Capability Bus。  
- **教学 / 演示**：只读角色 + 仿真通道，禁止真车写。

### 2.3 全能力面（Capability Surface）

能力分四层，**禁止跳层裸调**：

```
L0  Host Primitives     sin.* / 主进程 RPC（帧、DBC、工程、设备、命令）
L1  Domain Tools        各套件注册的 typed tools（读 OD、校验 EDS、COM pack…）
L2  Workflows           多步剧本（scan→apply node→SDO read；DBC lint→fix）
L3  Experience Hooks    UI 入口（Ask / Fix / Generate）绑定 L1/L2，不直连模型
```

AI Agent 是 **L3 编排器之一**，不是唯一入口；其他套件通过同一 L1 注册表消费 AI（解释、补全），也通过同一总线被 AI 调用。

---

## 3. 目标架构

### 3.1 逻辑视图

```
┌─────────────────────────────────────────────────────────────────┐
│ openbus.exe (C++ Qt 主进程)                                       │
│  Trace · Graphic · Device · DBCManager · PluginManager · Market │
│  Capability Broker (新增) · Policy Authority · Audit Sink         │
└───────────────┬───────────────────────────▲─────────────────────┘
                │ ZMQ JSON-RPC + FRAME PUB   │
┌───────────────▼───────────────────────────┴─────────────────────┐
│ sin_host.py（单一 Python 宿主进程）                                │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────────┐ │
│  │ Domain Suites│  │  ai-agent    │  │ Capability Registry    │ │
│  │ uds / canopen│  │ Orchestrator │  │ (in-host, process-wide)│ │
│  │ dbc / …      │  │ Chat + Roles │  │ tools / resources /    │ │
│  │              │  │ MCP optional │  │ prompts 声明与发现      │ │
│  └──────┬───────┘  └──────┬───────┘  └──────────▲─────────────┘ │
│         │ register tools  │ invoke              │                 │
│         └─────────────────┴─────────────────────┘                 │
│  Isolation: import firewall · suite module eviction · quota       │
└───────────────────────────────────────────────────────────────────┘
         │ MCP stdio / HTTP（可选对外）
         ▼
   Cursor / Claude Desktop / CI Agent
```

### 3.2 三条「打通」路径

| 路径 | 作用 | 主契约 |
|------|------|--------|
| **A. Plugin ↔ Host** | 读写 Trace/DBC/设备/命令 | 扩展现有 `sin.*` + `PluginManager::handleHostMessage` |
| **B. AI ↔ Domains** | Agent 调用套件能力；套件调用解释/生成 | **Capability Registry**（同宿主内存总线 + 可选主进程镜像） |
| **C. External ↔ openbus** | 外部 Agent 复用能力 | **MCP Server** 投影同一 Registry |

### 3.3 与「领域套件」策略的关系

- **不**再为 AI 拆一堆细插件。  
- **ai-agent** 保持平台级插件身份（市场独立条目）。  
- 每个领域套件在 `activate` 时向 Registry **声明**自己的 tools/resources；`deactivate` 时注销。  
- Agent **消费**套件工具面，必要时 `suite.open` + `goto` 打开专业 UI，而不是在聊天里重做 CANeds/CANdb++。

---

## 4. Capability Bus（核心新契约）

### 4.1 为什么需要

今天 Agent 的 `ToolRegistry` 是插件私有的。要「AI 用全软件」，必须有 **全宿主可见、可版本化、可鉴权** 的能力目录。

### 4.2 对象模型

```text
Capability {
  id:          "canopen.eds.validate"          # 稳定 ID
  provider:    "canopen-suite"                 # 插件 id
  kind:        tool | resource | prompt
  title:       "Validate EDS draft"
  description: "…"
  input_schema / output_schema                 # JSON Schema
  permission:  read | write | diag_write | flash
  tags:        ["eds", "cia306"]
  stability:   experimental | stable
  version:     "1.0.0"
}

Resource {
  id: "trace.selection" | "workspace.dbc" | "canopen.session.od"
  # 只读上下文，供模型 /prompt 注入，禁止隐式写
}
```

### 4.3 API（宿主内）

建议新模块：`plugins/_shared/capability_bus.py`（或 `sdk/sin/capabilities.py` 薄封装）：

| API | 说明 |
|-----|------|
| `register(plugin_id, caps[])` | `activate` 时调用 |
| `unregister(plugin_id)` | `deactivate` 时调用 |
| `list(filter?)` | Agent / MCP `tools/list` |
| `invoke(cap_id, args, ctx)` | 统一入口：Policy → 配额 → 审计 → provider handler |
| `get_resource(id)` | 只读快照 |

**硬性规则：**

1. 领域套件 **不得** `import` 其他套件的 `pages.*` / `session` 私有符号。  
2. AI **不得** 直接 `from canopen_suite…`；只走 `invoke`。  
3. 跨插件协作只允许：Capability Bus、`sin.commands`、文件工件、`state_store` 约定键。

### 4.4 主进程侧镜像（可选但推荐）

在 `PluginManager` 增加轻量 RPC：

- `capabilities.list`  
- `capabilities.invoke`（转发到宿主）  

好处：C++ UI（Trace 右键「Ask AI」）不必依赖 Python 窗体细节；审计可在主进程落一份。

---

## 5. 宿主打通（Plugin ↔ Main）

### 5.1 现有通道（保留）

| 通道 | 用途 |
|------|------|
| ZMQ ROUTER JSON-RPC | 控制面：`frames.*` `dbc.*` `workspace.*` `executeCommand` … |
| ZMQ PUB 二进制帧 | 数据面：测量中的 FRAME_BATCH |
| `registerCommand` | 菜单 / `sin.commands.execute` |
| `sin.ui` / 自建窗口 | 插件 UI |

详见 [`插件方案.md`](插件方案.md)。

### 5.2 为 AI-Native 必补的 Host RPC

| Method | 用途 | 优先级 |
|--------|------|--------|
| `device.getStatus` | 通道、bitrate、测量状态 | P0 |
| `plugins.list` | 已安装/已激活插件 | P0 |
| `plugins.activate` / `raise` | Agent 打开套件窗口 | P0 |
| `trace.query` | 按 ID/时间窗/条件取帧（避免只 getRecent） | P1 |
| `trace.getSelectionMeta` | 选中帧 + 解码缓存 | P1 |
| `capabilities.list/invoke` | 主进程入口 | P1 |
| `audit.append` | 统一审计汇入主进程 | P2 |

### 5.3 `bus_get_status` 纠偏

当前 Agent 工具返回 `"device": "unknown"`。P0 打通后，Analyst/Diagnostics 角色才能可靠拒绝「设备未开却发送」。

### 5.4 套件深度 Facade（替代「假 UDS」）

分两级：

| 级别 | 行为 | 适用 |
|------|------|------|
| **L-frame** | 拼 SF/直接 `frames.send`（现状） | 演示、无套件环境 |
| **L-suite** | `capabilities.invoke("uds.read_did")` → 套件 ISO-TP 会话等待响应 | 正式诊断 |

路线：先 Registry + L-frame 兼容；UDS/CANopen/OBD 逐步迁到 L-suite，并在 tool 描述中标明 `provider` 与超时语义。

---

## 6. 隔离模型（AI 与其它插件）

### 6.1 隔离目标（威胁模型）

| 威胁 | 缓解 |
|------|------|
| Agent 幻觉导致乱发帧 | Policy 默认 readonly；写操作 HITL；TX 速率限制 |
| 套件 bug 拖垮 Agent | Capability `invoke` 超时、异常捕获、结果大小上限 |
| Agent / 第三方工具扫私有模块 | Import firewall + 禁止跨套件 path 注入 |
| 密钥泄漏 | API Key 本地加密存储；审计剥离密钥；不进 git |
| 供应链（恶意 .opk） | 市场签名/哈希（演进）；能力声明审查 |
| 同进程内存破坏 | 长期：关键插件子进程；短期：软隔离 + 崩溃恢复宿主 |

### 6.2 分层隔离（务实路线）

**阶段 I（当前可立即强化）— Soft Isolation**

- 单一 Python 宿主保留（启动成本与 Qt 绑定现实）。  
- **Import firewall**：`sin_host` 加载插件时限制 `sys.path`；套件不得把对方目录加入 path。  
- **Module eviction**：已有 `_SUITE_LOCAL_TOPS` 驱逐，扩展到 capability handler 卸载。  
- **Policy 统一**：所有 `capabilities.invoke` 走同一 Policy Authority（级别来自用户会话，不来自调用方插件自称）。  
- **配额**：每插件每分钟 invoke 次数、结果字节数、TX 次数。  
- **审计**：`~/.openbus/audit/YYYYMMDD.jsonl` + 可选主进程副本。

**阶段 II — Trust Zones**

| Zone | 成员 | 权限 |
|------|------|------|
| `host-core` | Trace/DBC/workspace RPC | 由主进程实现 |
| `domain` | 各 `*-suite` | 只注册自己的 caps |
| `agent` | `ai-agent` | 可 list/invoke；不可改他人注册 |
| `external-mcp` | MCP 客户端 | 默认只读；写需二次 HITL |

**阶段 III — Hard Isolation（可选旗舰）**

- 高风险插件（刷写、安全测试）跑 **子进程** + 更窄 RPC。  
- 或 WASM/受限解释器跑「纯分析」工具。  
- 成本高，仅在安全产品线需要时启动。

### 6.3 AI 插件「特殊但不特权」

- **特殊**：唯一默认持有 Orchestrator、Provider 配置、多角色系统提示。  
- **不特权**：不能绕过 Policy；不能未经 Registry 调套件私有 API；崩溃不得要求主进程重启以外的特权恢复。

### 6.4 与「一个套件窗口」策略共存

C++ `PluginManager` 已对 `*-suite` 做互斥激活。AI 打开套件时：

1. `plugins.activate("uds-suite")`  
2. `capabilities.invoke("uds.…")` 使用该套件会话  
3. 需要 UI 时再 `raise` / `goto`  

Agent 自身窗口可与套件并存（非 `*-suite` 互斥集，或显式白名单）。

---

## 7. 一切可投喂：Context Attach（对标 Cursor Add to Chat）

### 7.1 产品原则

> **凡用户能看见、能选中、有分析价值的对象，均可右键「Add to AI Chat」。**  
> 投喂的是 **结构化引用（Attachment）**，不是把整份工程无脑塞进 prompt。  
> 聊天输入区用 **芯片（chip）** 展示引用；支持 `@` 补全、拖拽、多选批量添加、一键移除。

对标参考：

| 产品 | 机制 | openbus 借鉴 |
|------|------|----------------|
| **Cursor** | 右键 / `@` 引用文件、文件夹、Terminal、Diff、Chat | 引用模型、芯片 UI、多附件 |
| **Claude / ChatGPT** | 附件 + 项目知识 | 大附件摘要策略 |
| **VS Code Copilot** | Add to Chat / `#file` | 编辑器选区投喂 |

### 7.2 Attachment 统一模型

所有投喂物归一为：

```text
Attachment {
  id:           uuid
  kind:         file | folder | selection | trace_frames | trace_signal |
                dbc_message | dbc_signal | eds_object | od_entry |
                log_rows | output_rows | graphic_cursor | device_status |
                suite_page | artifact | custom
  title:        "TPDO1 0x1A00"          # 芯片上显示的短名
  uri:          "openbus://trace/frames?ids=…" | "file:///…" | "cap://…"
  mime:         "application/vnd.openbus.trace-frames+json"
  preview:      "3 frames · 0x123…"     # tooltip / 折叠摘要
  payload_ref:  # 不把大体量内联进消息；会话侧按需 resolve
    strategy:   inline | lazy | summarize
    bytes_hint: 1200
  provenance:   { source_plugin, window, ts }
  ttl:          session | pinned
}
```

**硬规则：**

1. **默认 lazy**：Trace 多行、大文件、目录只存 URI + 元数据；发送给模型前由 `ContextResolver` 展开并截断。  
2. **目录 ≠ 全量灌入**：文件夹附件展开为「树摘要 + 用户点名的子文件」；禁止一次塞进数万行（Cursor 也不整仓硬塞）。  
3. **敏感剥离**：密钥、完整刷写镜像默认不可投喂；Policy 可拦截。  
4. **可引用可撤销**：芯片关闭即从下一轮上下文移除；已发出历史轮次保留当时快照摘要。

### 7.3 投喂目录（覆盖面）

| 来源 | 右键菜单文案 | kind | 解析内容（示例） |
|------|--------------|------|------------------|
| 工程资源管理器 | Add to AI Chat | file / folder | 路径、语言、截断正文 / 目录树 |
| Trace 行（单/多选） | Add frames to AI | trace_frames | id、时间、DLC、数据、可选 DBC 解码 |
| Trace 信号 / Graphic 点 | Add signal to AI | trace_signal | 信号名、物理值、时间窗 |
| DBC 树报文/信号 | Add to AI Chat | dbc_* | 布局、起止位、因子、接收节点 |
| EDS / OD 对象 | Add object to AI | eds_object / od_entry | index:sub、类型、访问、默认值 |
| OUTPUT / 套件 Log | Add log to AI | output_rows | 时间、方向、PDU、Note |
| 校验/Lint 行 | Add finding to AI | artifact | level、规则、定位 |
| 设备状态栏 | Add bus status | device_status | 通道、bitrate、测量 on/off |
| 套件当前页 | Add page context | suite_page | 套件 id、页 id、可见选择 |
| 已生成报告/工件 | Add artifact | artifact | 路径 + 摘要 |

口号落地：**一切可投喂** = 上表可扩展；新 UI 只需实现 `IAttachable`（见下），不必改 Agent 内核。

### 7.4 平台契约：`IAttachable` + Context Inbox

```
任意视图 ──右键──► AttachmentFactory.build(selection)
                         │
                         ▼
              ContextInbox.push(attachment)     # 主进程或 sin_host 单例
                         │
                         ├── 若 AI 窗口未开：activate ai-agent + 打开当前会话
                         └── Chat 输入区渲染 chip；可选自动 focus 输入框
```

建议 API：

| API | 位置 | 作用 |
|-----|------|------|
| `sin.ai.attach(attachment\|dict)` | SDK 新模块 | 插件/套件投喂 |
| `ai.attach` / `ai.openWithAttachments` | Host JSON-RPC | C++ Trace/资源管理器投喂 |
| `ContextInbox` | `ai-agent` 或 `_shared` | 跨窗口队列；线程安全 |
| `ContextResolver.resolve(atts, budget)` | agent 侧 | 按 token 预算展开 / 摘要 |

C++ 侧：Trace、工程树、OUTPUT 统一挂 QAction「Add to AI Chat」→ RPC。  
Python 套件：表格/树 `customContextMenu` 复用 `_shared/ai_attach.py` 助手，避免每页复制粘贴。

### 7.5 聊天内 `@` 与芯片 UX

- 输入 `@`：弹出最近附件、打开文件、当前 Trace 选中、已注册 Resource。  
- 芯片：图标（按 kind）+ 短标题 + `×`；Hover 显示 preview。  
- 工具条：「Clear context」「Pin」；过多附件时折叠为「+N more」。  
- **Ask about this**（单对象立即开新线程并自动带一句「请分析以下对象」）与 **Add to Chat**（仅附加、用户自己写问题）分开，避免误触发烧 token。

### 7.6 与 Capability Bus 的关系

- Attachment = **只读上下文**（Resource 族）。  
- 真正改总线 / 改文件仍走 Capability `invoke` + Policy。  
- 「针对投喂对象修复」流程：Resolve 附件 → Manager Agent 规划 → 专科 Agent 调 tool → HITL → 结果再可作为 artifact 投喂回聊天。

### 7.7 横切入口（与投喂并列）

| 入口 | 行为 |
|------|------|
| **Add to AI Chat** | 附加到当前会话（默认） |
| **Ask about this** | 新线程 + 预填问题 + 附件 |
| **Explain** | 只读快捷意图（内部仍走 Attach + 固定 prompt） |
| **Fix** | Attach finding + 生成补丁建议 → HITL |
| **Generate** | 少附件 + 生成工件 |

### 7.8 套件赋能示例（投喂之后）

| 套件 | 典型投喂 → 分析 |
|------|-----------------|
| DBC Studio | 信号/报文 → 解释布局、冲突、生成补丁 |
| CANopen EDS | 对象 → 缺强制项、SDO abort 解释 |
| UDS | OUTPUT 否定响应 → 会话建议（拒绝爆破） |
| Trace / Graphic | 多帧/信号 → 谁在刷屏、周期异常 |
| AUTOSAR / EtherCAT | 校验行 → ARXML/ESI 修复建议 |
| Market / 工程树 | 目录/插件 → 推荐安装与入门路径 |

主进程：Trace「Summarize selection」、连接失败向导、空工程 checklist — 全部先 Attach 再问 Manager。

---

## 8. 多智能体编排：调研结论与选型（不造轮子）

### 8.1 我们需要什么

| 需求 | 说明 |
|------|------|
| 任务分解 | 用户一句话 → 子任务图（读 Trace、查 DBC、调 UDS、写报告） |
| 调用下级智能体 | Analyst / Diagnostics / EDS / DBC / Safety 等专科 |
| 给出答案与操作结果 | 最终答复 + tool/capability 轨迹 + 可选 HITL 操作结果 |
| 多 Provider | 已有 OpenAI-compatible（含 Ollama） |
| 安全 | 与现有 Policy / HITL / 审计对齐 |
| 体积 | 可随 openbus 插件分发，避免巨型依赖地狱 |

### 8.2 2025–2026 主流开源 / SDK 对比

| 框架 | 形态 | 优势 | 风险 / 代价 | 结论 |
|------|------|------|-------------|------|
| **OpenAI Agents SDK**（Python） | Agent + **handoffs** + **Agent.as_tool()** + MCP + guardrails | 官方多 Agent 范式清晰；与 OpenAI-compatible tool-loop 接近；文档把「经理调专科 / 交接专科」写死 | 偏 Responses/OpenAI 生态；需验证对纯本地 Ollama 的适配成本 | **编排首选候选** |
| **LangGraph**（LangChain Inc.） | 显式状态图、checkpoint、HITL 一等公民 | 生产向事实标准之一；模型无关；复杂分支/重试强 | 学习曲线与依赖面更大；打包体积 | **复杂长流程 / 强 HITL 图的备选与增强层** |
| **CrewAI** | 角色团队（Researcher/Writer…） | 最快搭出「多角色」Demo | 生产态控制、checkpoint、确定性弱于 LangGraph | **仅原型，不进发行版主路径** |
| **AutoGen / AG2** | 对话式多 Agent | 研究向灵活 | Microsoft 已转向新 Agent Framework，AutoGen 维护态；新项目不建议押注 | **不采用** |
| **Claude Agent SDK** | Anthropic 原生环 | Claude 生态完整 | 锁 Anthropic；与现有多 Provider 战略冲突作唯一底座 | **可选适配器，不作唯一底座** |
| **Microsoft Agent Framework** | AutoGen+SK 收敛 | .NET/企业向 | 与当前 Python 插件栈不完全同构 | **观望** |
| **自研 Orchestrator（现状）** | 单 Agent ReAct | 已落地、可控 | 继续堆 handoff/计划/子代理 = **重复造轮子** | **降级为薄适配层或淘汰** |

权威能力锚点（OpenAI Agents SDK）：

- **Agents as tools**：经理 Agent 保持会话控制，用 `Agent.as_tool()` 调用专科，适合「综合回答 + 汇总操作结果」。  
- **Handoffs**：分流后由专科接管本轮，适合「诊断会话交给 Diagnostics」。  
- 二者可组合；工具面可接 **MCP**（与本文 §9 一致）。

参考：[Agent orchestration](https://openai.github.io/openai-agents-python/multi_agent/)、[Handoffs](https://openai.github.io/openai-agents-python/handoffs/)。

### 8.3 决策（写入方案，避免摇摆）

| 决策 | 选择 |
|------|------|
| **是否自研多 Agent 内核** | **否**。停止扩展自研 ReAct 编排器的「子代理 / 计划图」能力。 |
| **默认编排运行时** | **OpenAI Agents SDK**：Manager（编排）+ 专科 Agents（as_tool / handoff）。 |
| **工具从哪来** | 全部来自 **Capability Bus**（及 L0 `sin.*`），包装成 SDK Tool；禁止专科 Agent 直 import 套件。 |
| **长事务 / 强断点 HITL** | 若 SDK 会话态不够：对「刷写审批流、多步台架剧本」叠加 **LangGraph** 子图，而不是第三套自研状态机。 |
| **CrewAI / AutoGen** | 不进主路径。 |
| **现有 `orchestrator.py`** | Phase A：Adapter 包一层 Agents SDK；Phase B：删除重复 tool-loop 逻辑。 |

### 8.4 目标运行时拓扑

```
用户问题 + Attachment chips
        │
        ▼
┌───────────────────┐
│  Manager Agent    │  ← OpenAI Agents SDK（或兼容 Runner）
│  分解任务 / 汇总   │
└─────────┬─────────┘
          │ as_tool / handoff
    ┌─────┼─────┬──────────┬─────────┐
    ▼     ▼     ▼          ▼         ▼
 Analyst Diag  DBC/EDS   TestEng   Safety
    │     │     │          │         │
    └─────┴─────┴────┬─────┴─────────┘
                     ▼
            Capability Bus.invoke
                     ▼
         Policy + HITL + Audit + sin.* / suites
```

专科 Agent = **薄系统提示 + 允许的 cap 白名单**，不是第二套聊天产品。  
SafetyOfficer：只读审计 + 拒绝 flash/爆破类 handoff。

### 8.5 与「投喂」的结合

1. Manager 系统提示声明：优先使用用户芯片中的 Attachment，禁止无视投喂空谈。  
2. 每个 Attachment resolve 后注入为带 `uri` 的消息块或 tool 可读 Resource。  
3. 子 Agent 只接收与任务相关的附件子集（handoff `input_filter` / 显式传参），防止上下文膨胀——与 Cursor Explore 子代理「子上下文」同思路。

### 8.6 打包与依赖策略

- `ai-agent` 插件可选依赖：`openai-agents`（编排）；重流程再拉 `langgraph`。  
- 无 API Key / 离线：Manager 仍可跑本地 Ollama（OpenAI-compatible）；若 SDK 某版本强绑云端，则保留 **最小兼容 Runner**（仅 tool-loop，无多 Agent）作降级，而不是再写框架。  
- 许可证：优先 Apache/MIT 系；引入前在本文件决策表追加一行。

---

## 8a. Agent 插件演进（原 §8 对齐）

| 组件 | 路径 | 演进 |
|------|------|------|
| Orchestrator | `agent/orchestrator.py` | → Agents SDK Runner 适配器 |
| ToolRegistry | `tools/registry.py` | → Bus Tool 适配；本地 fallback |
| Policy | `tools/policy.py` | 平台 Policy Authority（所有 Agent 共用） |
| ChatWindow | `chat_window.py` | 芯片栏、`@`、接收 ContextInbox |
| Facades | `tools/plugin_facades.py` | 迁到各套件 `register_capabilities()` |
| Session | `agent/session_store.py` | 附件快照 + 审计对齐 |

角色：Analyst / Diagnostics / TestEngineer / SafetyOfficer（映射为专科 Agent）。  
级别：`readonly` → `tx_allowed` → `diag_write` → `flash`。细节见 [`aiagent.md`](aiagent.md)。

---

## 9. MCP：对外同一能力面

### 9.1 原则

**一个 Registry，两种投影：**

- 对内：Function Calling schema（OpenAI-compatible）  
- 对外：MCP `tools/list` / `tools/call`（stdio 或 127.0.0.1 Streamable HTTP）

### 9.2 安全

- MCP 默认映射到 `readonly`  
- 写工具需环境变量或 UI 显式开启，并仍走 HITL（本地弹窗或审批文件）  
- 不把 API Key 暴露给 MCP 客户端  

### 9.3 价值

- Cursor / Claude Desktop / 自建 CI 与桌面 Agent **同一套总线工具**  
- 避免「IDE 内一套、外部又封装一套」的双轨腐烂  

---

## 10. 数据、记忆与评测

### 10.1 上下文分层

| 层 | 内容 | TTL |
|----|------|-----|
| Turn | 当前用户句 + 最近 tool 结果 | 会话内 |
| Thread | 摘要、当前套件、Node-ID、DBC 路径 | 会话 |
| Workspace | 工程文件索引、最近报告 | 工程生命 |
| Org（可选） | 内网手册 RAG | 配置启用 |

### 10.2 RAG（Phase 后置）

优先：**可引用的总线事实**（帧、解码、校验结果）＞ PDF 手册。手册 RAG 仅在企业版/内网索引开启。

### 10.3 Eval

- 黄金对话集：负载分析、DID 读、EDS 校验、拒绝刷写  
- 指标：工具选择正确率、越权尝试拦截率、幻觉率（无 tool 却断言总线事实）  
- CI：`ScriptedLLM` + 假 Capability provider（已有 mock 测试可扩展）

---

## 11. 安全与合规清单（发布门禁）

- [ ] 默认 Policy = readonly  
- [ ] 一切 write/diag/flash 有 HITL 与审计  
- [ ] API Key 不进审计、不进报告、不进市场包  
- [ ] Capability 声明含 permission；未声明不可 invoke  
- [ ] 结果截断（防 prompt 灌入超大 Trace）  
- [ ] 测量未运行时禁止「已验证上线」类结论  
- [ ] SafetyOfficer 回归用例必过  
- [ ] 外部 MCP 默认只读  

---

## 12. 分阶段路线图

### Phase A — 平台地基 + 投喂 MVP（1–2 迭代）

1. `capability_bus`（register / list / invoke / unregister）。  
2. Host RPC：`device.getStatus`、`plugins.list`、`plugins.activate`、**`ai.attach`**。  
3. **Context Attach MVP**：Trace 多选、OUTPUT 行、工程文件右键 → Add to AI Chat → 芯片。  
4. `ai-agent`：ContextInbox + chip UI；ToolRegistry 改 Bus 适配。  
5. 引入 **OpenAI Agents SDK** 作 Manager 试点（单专科 as_tool 即可）；自研 loop 降级。  
6. 2 个套件真 caps：`uds-suite`、`canopen-suite`。  
7. 审计 JSONL；修正「只读」文案。

### Phase B — 深度投喂 + 多 Agent

1. 投喂覆盖：DBC 树、EDS/OD、Graphic 点、目录（树摘要）、Lint 行。  
2. `@` 补全；Ask about this / Explain / Fix。  
3. Manager + Analyst / Diagnostics / EDS·DBC / Safety 专科（handoff 或 as_tool）。  
4. Plan 模式；`trace.query`。  
5. 删除重复自研 tool-loop。

### Phase C — MCP 与外部生态

1. Registry → MCP；外部 Cursor 可调同一工具面。  
2. CI headless 只读分析。  
3. 附件 URI 可被外部客户端解析的安全子集。

### Phase D — LangGraph 增强与硬隔离（按需）

1. 长台架/刷写审批流用 LangGraph 子图（仍不自研状态机）。  
2. 高风险工具子进程化；企业 RAG；签名市场。

**映射：** A≈打通+投喂；B≈多 Agent；C≈原 MCP Phase；D≈安全旗舰。

---

## 13. 关键决策（写入本方案，避免摇摆）

| 决策 | 选择 | 理由 |
|------|------|------|
| AI 形态 | 工具型多 Agent + 场景投喂，非纯聊天 | 总线事实必须可绑定 |
| 上下文 | **一切可投喂** / Attachment + chip / `@` | 对标 Cursor，深度绑定 |
| 能力发现 | 统一 Capability Bus | 消灭硬编码 facade |
| **编排内核** | **OpenAI Agents SDK**（主）；LangGraph（长 HITL 图） | **不重复造轮子** |
| 不采用 | 自研多 Agent、CrewAI 主路径、AutoGen 新项目 | 维护与生产成熟度 |
| 隔离 | 先软隔离 + 策略，再按需硬隔离 | 单宿主 Qt 现实 |
| 跨插件调用 | 只允许 Bus / commands / 文件 / Attach | 防止套件纠缠 |
| Computer Use | 不做主路径 | 脆、慢、难审计 |
| 刷写 / 爆破 | 默认拒绝 | 安全底线 |
| 大目录投喂 | 摘要 + 按需展开，禁止整仓灌入 | 上下文窗口现实 |
| 文档语言 | 架构 Markdown 中文；代码英文 | 仓库规范 |

---

## 14. 成功标准（可验收）

1. **发现性**：`capabilities.list` 列出稳定领域工具。  
2. **投喂**：Trace / 文件 / OUTPUT / DBC 或 EDS 至少四类可 Add to AI Chat，芯片可见可删，下一轮模型能引用其内容。  
3. **闭环**：自然语言「读水温 DID」走 L-suite，报告含真实响应。  
4. **多 Agent**：Manager 能分解任务并调用 ≥2 个专科 Agent，轨迹可审计。  
5. **不造轮子**：发行版编排依赖声明含 Agents SDK（或文档记录的等价 Runner），无第二套自研 handoff 实现。  
6. **隔离**：停用套件后 caps 消失；Agent 无法 import 私有模块。  
7. **安全**：readonly 下写工具不可见；越权被拒并审计。  
8. **对外**：MCP list 与内部 list 一致（可只读子集）。

---

## 15. 文档与代码索引

| 资源 | 说明 |
|------|------|
| [`aiagent.md`](aiagent.md) | Agent 插件功能规格与 Phase 细节 |
| [`插件方案.md`](插件方案.md) | ZMQ / sin SDK 传输契约 |
| [`Plugin_Domain_Suites.md`](Plugin_Domain_Suites.md) | 领域套件地图；AI 消费工具面 |
| [OpenAI Agents SDK — orchestration](https://openai.github.io/openai-agents-python/multi_agent/) | handoff / as_tool 权威文档 |
| [OpenAI Agents SDK — handoffs](https://openai.github.io/openai-agents-python/handoffs/) | 专科交接 |
| [Cursor — @ mentions](https://cursor.com/help/customization/context) | 投喂 UX 对标 |
| `plugins/ai-agent/` | 当前实现 |
| `sdk/sin/` | 宿主 SDK |
| `scripts/sin_host.py` | Python 插件宿主 |
| `src/core/plugin/pluginmanager.cpp` | 主进程 RPC 汇聚 |

---

## 16. 结语

AI-Native 的关键不是「更会聊天」，而是：

> **把看得见的一切变成可引用的附件；  
> 把专业能力变成可调用的工具；  
> 用成熟开源多 Agent 框架做任务分解与专科协作；  
> 用策略与隔离保证事故半径可控。**

openbus 差异化在 **总线领域深度 + 一切可投喂 + Capability Bus**，不在再写一套 Agent 操作系统。按 Phase A → D 推进：先让右键投喂与 Manager 跑通，再铺专科与 MCP。

---