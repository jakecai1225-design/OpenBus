# openbus — AI Native 总线工作台方案

> 文档：`doc/ai.md`（产品定位 + 平台架构总纲）  
> 配套：Agent 插件细节 [`aiagent.md`](aiagent.md) · 实施勾选 [`ai_implementation_plan.md`](ai_implementation_plan.md) · 传输 [`插件方案.md`](插件方案.md) · 领域套件 [`Plugin_Domain_Suites.md`](Plugin_Domain_Suites.md)  
> 日期：2026-09-20  
> **工程约束（硬）**：最小化改动主进程与插件框架；AI 推理**永不阻塞**采集热路径；AI 是协作者不是黑盒决策者。

---

## 0. 产品定位一句话

**openbus 是 AI Native 的工业总线分析工作台**（对标 CANoe 级采集 / 解析 / 诊断能力）。

- **架构层**：数据流、Capability Bus、Context Pool、**活动上下文**、**知识沉淀**、任务编排从底层按 AI 设计。  
- **交互层**：完善的 AI 插件工作台（对标 Cursor）承载聊天与配置。  
- **成长层**：始终知道用户在干啥；专业与个人经验沉淀 → **越用越聪明、越用越懂用户**。

二者（+成长）同时成立：

> **底层 Native，表层有一等公民工作台，知识与习惯越用越厚。**  
> 拒绝「主体零改造 + 孤立侧栏」；**必须做好**工作台体验与可管可控的知识库。

### 0.1 一句话判断标准

| | 外挂 AI（拒绝） | AI Native + 一等公民工作台（我们要的） |
|--|----------------|----------------------------------------|
| 架构 | 主体不变，能力进不了数据对象 | Context Pool / Bus / 旁路洞察原生存在 |
| 能力 | AI 不能操纵内部对象 | AI 经 Bus 读写总线数据、视图、工程资源（经策略） |
| 工作流 | 人点菜单，偶发问 AI | 声明式目标 + 人机协同；投喂/选区自动进上下文 |
| **交互面** | 简陋侧栏、无配置深度 | **`ai-agent` = Cursor 级工作台**（见 §0.4） |
| openbus 现状 | 有窗但能力偏薄 | 目标：Native 底座 + 工作台体验一并做强 |

### 0.2 工业车载硬约束（不可照搬消费级）

1. **AI = 协作者**，不是自动决策者；结论必须带**置信度 + 证据溯源**；低置信度降级人工。  
2. **领域边界不变**：硬件/协议插件、领域套件、**AI 推理/工作台插件**三类解耦。  
3. **性能红线**：CAN FD 等实时采集路径**禁止**被推理阻塞；AI 只跑异步/旁路线程。  
4. **一键回退**：可关闭 AI 层，回到纯传统 Trace / 套件视图。  
5. **可审计可复现**：输入、prompt、工具轨迹、模型输出、置信度落盘。

### 0.3 最小改动原则（怎么变成 Native 而不大拆）

不重写 `PluginManager` / ZMQ / 套件壳。在**现有插件化骨架上叠加四条薄带**：

| 薄带 | 作用 | 落点（尽量不碰热路径） |
|------|------|------------------------|
| **Context Pool** | 统一「可被 AI 看见的」工程/总线上下文 | `_shared/ai_attach` + 旁路订阅帧摘要 |
| **Activity + Knowledge** | 知道用户在干啥；专业/个人经验沉淀 | 旁路活动事件 + 本机/工程知识层 |
| **Capability Bus** | AI 与套件互调的唯一契约 | `_shared/capability_bus` |
| **AI 工作台 + 推理插件** | Cursor 级聊天/配置 + 可替换模型 | **`ai-agent`** + 未来 `ai-*` |
| **Experience Hooks** | Add to Chat / Ask / 证据 / 记住 | Trace/套件 → 汇入工作台 |

主进程只增加必要通知/查询；采集 PUB 副本给 AI，不插入发送环。

### 0.4 AI 工作台（`ai-agent`）— 对标 Cursor 的交互与配置

架构 Native **不取消**独立 AI 窗口；相反，**`ai-agent` 必须做成产品级 Agent 工作台**，承担几乎全部「人对 AI 说话 / 配模型 / 看轨迹」的体验。全软件其它表面（Trace、套件）通过 **Add to Chat / Ask** 把上下文**送进这座工作台**，而不是每页自建一套聊天。

| 模块 | 对标 Cursor 的能力 | openbus 要点 |
|------|-------------------|--------------|
| **Chat** | 多轮对话、流式输出、停止生成 | 消息流 + 附件芯片 + 证据块可点回 Trace/DBC |
| **Composer / Agent 模式** | 计划 → 执行工具 → 汇总 | Manager + 专科；Plan 可先批后跑 |
| **@ / 附件** | `@file`、选区、终端 | Attachment 芯片；一切可投喂汇入此窗 |
| **Models / Provider** | 选模型、改 API、本地/云端 | OpenAI-compatible；Ollama/云端切换；密钥本地存 |
| **Rules / Roles** | 项目规则、角色 | Analyst / Diagnostics / …；工程级 rules 预留 |
| **Tools 可见性** | 工具列表与权限 | 当前 Policy 下 Bus 工具目录；HITL 审批队列 |
| **History / 会话** | 历史线程、导出 | 会话列表、报告导出、审计只读查看 |
| **Settings** | 完整设置页 | Provider、策略级别、TX 限额、洞察开关、MCP、隐私 |
| **Usage / 状态** | 用量与上下文占用 | token/步数提示、附件预算、测量是否在跑 |

**布局建议（工作台内，仍遵守套件 chrome 哲学）：**

- 主区：对话 + 芯片栏（任务第一）。  
- 可折叠：Tool Trace / 证据 / Plan。  
- 设置：独立「Settings」页或窗内页签，**不要**把 Provider 表单永久占满顶栏（可收纳）。  
- 全软件其它处：只保留轻量入口（右键 Add to Chat、Ask），**配置与长对话集中在 `ai-agent`**。

详细交互规格继续写在 [`aiagent.md`](aiagent.md)；本文件只定「工作台是一等公民」的产品边界。

**壳（Windows，已定）：** 领域套件继续纯 PyQt。AI Chat UI 为 Web（`webui/dist`）经本机 bridge；**B** = 系统 Edge `--app` 嵌入插件窗（无 Qt WebEngine）；**A** = 系统浏览器兜底。编排优先 `openai-agents`，MSYS2 无轮时用本地 Orchestrator。**Tauri 第二宿主延后**，不阻塞当前交付。

### 0.5 越用越聪明（硬产品目标）

AI Native 的最终体感不是「会聊天」，而是：

> **软件始终知道用户在干什么；专业知识与个人习惯不断沉淀；同一项目上越用越懂、越问越准。**

三层必须同时成立：

| 层 | 含义 | 没有它会怎样 |
|----|------|--------------|
| **活动上下文** | 知当前工程、打开的 Trace/套件页、选中对象、最近操作、测量状态 | 每次提问都要重复背景 → 外挂感 |
| **专业知识库** | 协议/DBC/EDS/故障案例/本社规范可检索 | 模型空转猜信号名 |
| **经验沉淀** | 纠错、采纳的建议、成功剧本、用户偏好回流知识层 | 用一百次仍像第一次见面 |

隐私默认：**本地优先**；不上云除非用户显式开启同步。原始高速帧不进知识库，只进摘要/指纹/案例卡片。

---

## 1. AI Native 核心特性 → openbus 映射

### 1.1 底层架构

#### A. 统一数据 Ingestion（上下文池，不是孤岛文件）

| 传统 | AI Native（目标） | openbus 落地 |
|------|-------------------|--------------|
| Trace / DBC / 报告互不通，靠手工导入 | 采集流、描述文件、**用户操作与标注**进入同一 **Context Pool** | Attachment + Resource URI + **Activity Timeline**；测量旁路采样；描述文件注册为本体 |
| 问 AI 要反复粘贴 | 模型一次可读关联切片 + **当前在干啥** | `resolve` + `activity.snapshot` + Capability |

**不做**：把全量高速帧灌进 LLM / 向量库。  
**要做**：引用 + 摘要 + 按需展开；活动事件异步落盘；热路径只写 CaptureLog。

#### B. 可扩展推理插件体系（两类插件）

```
┌─ 硬件 / 协议 / 领域套件 ─┐     ┌─ AI 工作台 + 推理插件 ──────┐
│ 驱动 · Trace · UDS …     │     │ ai-agent（Cursor 级交互面） │
│ canopen / dbc / …        │◄───►│ Chat · Settings · Tools    │
│ 采集、解析、专业 UI       │ Bus │ + 可选 anomaly 等专科插件   │
└──────────────────────────┘     └────────────────────────────┘
```

- **业务逻辑与模型解耦**：工作台可切换本地/云端 Provider；领域套件不绑死 LLM。  
- **交互集中、能力分散**：长对话与配置只在 `ai-agent`；能力在 Bus / Context Pool / Knowledge。  
- **离线优先策略**：轻量洞察与检索本地；重生成可云端（可配置）。

#### C. 活动上下文：AI 始终知道「用户在干啥」

不是只靠用户右键投喂。系统在旁路维护一份 **Activity Snapshot**（可关、可脱敏），供每次推理自动注入短摘要：

| 信号 | 示例 | 来源（最小改动） |
|------|------|------------------|
| 工程 | 工程路径、已加载 DBC/EDS 列表 | `workspace.*` / 工程状态 |
| 视图 | 当前 Trace 页、Graphic、激活套件与页 id | PluginManager + 套件 `goto` 钩子（轻量事件） |
| 选区 | 选中帧/信号/OD 行 | 已有选区 API + Attach |
| 设备 | 通道、bitrate、测量 on/off | `device.getStatus`（待补） |
| 最近操作 | 打开文件、改过滤、SDO 读、保存 EDS | 异步 **Activity Log**（环形缓冲，非热路径） |
| 会话意图 | 当前 Role、Policy、未决 HITL | `ai-agent` 状态 |

注入原则：

- 默认只给模型 **200–800 token 的快照摘要**，大对象仍靠 Attach / uri。  
- 用户可「锁定上下文」或「清除活动记忆」。  
- **不问自知**：打开工作台或发送消息时自动带上快照，无需每次口述「我在看 CANopen EDS」。

#### D. 专业知识库：分层沉淀，越用越懂

```
┌──────────────────────────────────────────────────────────┐
│ L-session   本会话附件、临时结论、未提交纠错                 │
├──────────────────────────────────────────────────────────┤
│ L-project   本工程：DBC/EDS 摘要、常用 Node、过滤偏好、      │
│             成功诊断剧本、用户纠错卡片、报告索引               │
├──────────────────────────────────────────────────────────┤
│ L-user      跨工程：角色习惯、常用 Provider、禁区（勿自动TX） │
├──────────────────────────────────────────────────────────┤
│ L-domain    产品/组织：CiA/UDS 本体、内置手册、故障案例库     │
└──────────────────────────────────────────────────────────┘
         ▲ 检索（关键词 + 后期向量）    │ 写入（纠错/采纳/标注）
         └──────── Knowledge Bus ───────┘
```

| 知识类型 | 内容 | 写入触发 | 使用方式 |
|----------|------|----------|----------|
| **领域本体** | 信号/对象/服务语义（来自 DBC/EDS/OD 解析） | 加载/保存描述文件 | 解码与解释优先查本体 |
| **案例卡** | 「某 ID 刷屏→滤波」「某 abort→原因」 | 用户确认「记住这次」或采纳建议 | 相似场景检索 |
| **纠错卡** | AI 判错 → 人工改正 | 工作台「纠正并沉淀」 | 抑制重复误判 |
| **剧本** | 成功声明式任务的步骤摘要 | 任务成功且用户允许 | 下次同类目标少走弯路 |
| **偏好** | 默认 Role、Policy、常用套件 | Settings / 隐式统计（可关） | 系统提示与默认工具集 |
| **规范/手册** | PDF/内网文档（可选） | 显式导入 | 向量检索（Phase D） |

Capability 预留：`knowledge.search` / `knowledge.remember` / `knowledge.forget`（forget 满足隐私与误记删除）。

#### E. 反馈闭环（越用越聪明的发动机）

```
AI 建议 → 用户采纳 / 修改 / 拒绝
              │
              ├─ 采纳 → 可写入 L-project 案例或剧本
              ├─ 修改 → 纠错卡（原结论 + 正确结论 + 证据 uri）
              └─ 拒绝 → 负反馈（降低同类建议权重，不强制上传）
```

外挂聊天「纠了也白纠」；Native 要求：

1. 纠错**默认可沉淀到本机/本工程**；  
2. 下次 `knowledge.search` 优先命中；  
3. 工作台可浏览/编辑/删除知识条目（人是最终主人）。

---

### 1.2 数据与能力

| 特性 | 含义 | openbus |
|------|------|---------|
| **主动洞察** | 数据找人，非人翻表 | 异步 anomaly；告警可一键沉淀为案例 |
| **自然语言一等公民** | NL 可驱动内部对象 | 声明式任务 + HITL |
| **多模态** | 表、曲线、文本、截图 | Attachment kinds；Phase B+ |
| **记得住** | 跨会话/跨天仍懂本工程 | L-project 知识 + 活动快照 |
| **越问越准** | 纠错与采纳改变下次行为 | 反馈闭环 → knowledge.* |

示例声明式目标：

> 「分析本次启动是否总线超时并导出报告」  
> → 过滤时间窗 → 提信号 → 周期统计 → 结论 + 报告；若用户点「记住」，写入本工程案例卡。

---

### 1.3 交互体验（与 UI 哲学一致）

| 特性 | 要求 | openbus |
|------|------|---------|
| **渐进式协同** | 结论带置信度与证据；低置信降级 | `confidence` + `evidence[]` 可点回 |
| **上下文感知** | 知当前 Trace/DBC/选中/**正在操作什么** | Activity Snapshot 自动注入 + Attach |
| **知识可管** | 用户能看到「软件记住了什么」 | 工作台 Knowledge 页：浏览/删/导出 |
| **自适应 UI** | 任务变则推荐视图 | 弱自适应：建议 `goto` / 打开套件 |
| **一切可投喂** | 对标 Cursor Add to Chat | 右键汇入工作台芯片 |

UI 哲学延续：主区是任务；工作台集中对话与知识；不每页复制聊天壳。

---

### 1.4 工程 / 产品

| 特性 | openbus |
|------|---------|
| 可审计可复现 | audit + 会话报告；知识写入同样留痕 |
| 混合推理 | local_realtime / cloud_heavy |
| 声明式任务 | Agents SDK Manager + 专科 |
| 隐私 | 知识默认本机/本工程；云同步显式开关 |
| 可关可忘 | 关 AI 层；`knowledge.forget`；清活动记忆 |

## 2. 目标架构（在现有骨架上长出来）

### 2.1 逻辑图

```
                    ┌─────────────────────────────────────────┐
                    │  Experience：Ask / Attach / 证据卡片      │
                    │  （Trace 右键 · 套件 · Chat 芯片）         │
                    └───────────────────┬─────────────────────┘
                                        │
┌───────────────────────────────────────▼───────────────────────────────────────┐
│                         AI Native 层（旁路，可关闭）                              │
│  Context Pool · Activity Snapshot · Knowledge（L-session…L-domain）            │
│  Capability Bus · Policy/HITL/Audit · Agent 编排（开源 SDK）· 洞察队列           │
└───────────────┬───────────────────────────────▲───────────────────────────────┘
                │ 只读订阅 / 活动事件 / attach    │ invoke / remember / search
┌───────────────▼───────────────────────────────┴───────────────────────────────┐
│ 既有 openbus 内核（最小改动）                                                    │
│  Capture/Trace/Graphic │ Device │ DBCManager │ PluginManager │ ZMQ            │
│  Domain Suites │ Drivers                                                     │
└───────────────────────────────────────────────────────────────────────────────┘
```

**采集热路径**：驱动 → CaptureLog → Trace。  
**AI 路径**：PUB 副本 / 活动事件 / Attach → Pool + Knowledge → 异步推理。分离。

存储建议（最小改动、本地优先）：

| 数据 | 位置 |
|------|------|
| 活动环形缓冲 | 内存 + 可选 `~/.openbus/activity/` |
| L-project 知识 | 工程目录 `.openbus/knowledge/` 或 state_store |
| L-user 偏好 | `~/.openbus/ai-agent/user_memory.json` |
| L-domain | 安装包/只读资源 + 可选企业包 |
| 审计 | `~/.openbus/audit/` |
### 2.2 能力分层（禁止跳层）

```
L0  Host primitives     sin.* / 少量新增 RPC（ai.attach, device status…）
L1  Domain tools        套件注册的 Capability（真会话，非假拼帧）
L2  AI inference tools  异常检测、解读、生成（AI 插件注册）
L3  Workflows           Manager 声明式编排 L1+L2
L4  Experience          Attach / Ask / 证据 UI
```

### 2.3 与「领域套件」策略的关系

- 继续：**少而完整的领域套件**，不拆细插件。  
- **ai-agent**：平台级 **AI 工作台插件**（市场独立条目）= Cursor 级聊天/配置/轨迹 UI **+** 编排引擎；不是简陋侧栏，也不是第 N 个业务仿制品。  
- 全软件入口（右键 Add to Chat 等）**汇入**该工作台会话，避免每页复制聊天壳。  
- 未来可增加专用 AI 推理插件（如 `ai-anomaly`），一律走 Capability Bus。

---

## 3. Context Pool 与「一切可投喂」

### 3.1 池内对象

| 来源 | 进入方式 | 备注 |
|------|----------|------|
| Trace 选中 / 时间窗 | Attach / 自动选区 | 大窗口只存摘要 + uri |
| **活动快照** | 自动（旁路事件） | 当前页/套件/设备/最近操作 |
| 旁路帧统计 | 异步采样 | 主动洞察；可关 |
| DBC/EDS/ARXML/ESI | 工程资源注册 | 本体 |
| OUTPUT / Log | Attach | |
| 用户标注 / 纠错 / 采纳 | **写入 Knowledge** | 越用越聪明 |
| 报告 / 工件 | Attach + 索引 | |

### 3.2 Attachment 与 Cursor 对标

统一 `Attachment{…}`；Chat 芯片；`@`。原则：**投喂引用，不灌整仓**。

### 3.3 证据溯源（工业关键）

```text
claim / confidence / evidence[] / actions_suggested[]
```

点击 evidence → Trace / DBC / EDS。成功路径可一键「沉淀为案例」。

### 3.4 活动事件最小集（实现时可增量）

| 事件 | 何时发 | 载荷（摘要） |
|------|--------|--------------|
| `view.focused` | 切换 Trace/套件页 | plugin_id, page_id |
| `selection.changed` | 选中变化（防抖） | kind, count, uri |
| `resource.opened` | 打开 DBC/EDS/… | path, type |
| `measure.state` | 测量启停 | on/off, bitrate |
| `tool.invoked` | Capability 成功 | cap_id, ok |
| `user.feedback` | 采纳/纠正/拒绝 | 指向 knowledge id |

全部异步、可采样丢弃；**禁止**在帧中断里同步写盘。

---

## 4. Capability Bus（AI 操纵软件的合法入口）

- 套件 / AI 插件 `activate` → `register`；`deactivate` → `unregister`。  
- AI **只** `list` / `invoke`；禁止跨插件裸 import。  
- Policy：`readonly` → `tx_allowed` → `diag_write` → `flash`；写操作 HITL。  
- 实现：`plugins/_shared/capability_bus.py`（已铺）。

---

## 5. Agent 编排（不重复造轮子）

| 决策 | 选择 |
|------|------|
| 多 Agent 内核 | **OpenAI Agents SDK**（Manager + `as_tool` / `handoff`） |
| 长 HITL 图 | 按需 **LangGraph** 子图 |
| 不采用 | 自研多 Agent、CrewAI 主路径、AutoGen 新项目 |
| 工具来源 | Capability Bus + L0 `sin.*` |
| 专科 | Analyst / Diagnostics / EDS·DBC / Test / Safety |

现有自研 `Orchestrator`：降级为适配/fallback，不再扩展 handoff。

---

## 6. 主动洞察（异步，可关）

```
帧 PUB 副本 → 滑动窗特征（周期、跳变）→ 本地规则/小模型
         → 告警事件 → OUTPUT + 可选 Trace 高亮 + 可一键 Attach 问 Manager
```

- 默认关闭或低灵敏度；用户显式开启。  
- **绝不**在 `onFrame` 热回调里跑大模型。

---

## 7. 主进程 / 框架改动边界（白名单）

### 7.1 允许的最小增量

| 项 | 理由 |
|----|------|
| `ai.attach` | 投喂进工作台 |
| Trace 右键 Add to AI Chat | Experience |
| 可选轻量 `activity.emit`（防抖旁路） | 活动上下文 |
| 可选 `device.getStatus` / `plugins.list` | 快照完整性 |
| 可选旁路帧统计订阅（已有 PUB） | 主动洞察 |

### 7.2 明确不做（本阶段）

- 重写 PluginManager / 每插件进程沙箱（除非 Phase D）。  
- 在采集线程插入推理或知识写入。  
- 未经同意上传用户知识到云。  
- Computer Use；无人值守刷写/爆破。  
- 为 AI 再拆一堆细业务插件。

---

## 8. 外挂 → Native 演进路线

| 阶段 | 主题 | 进展 | 状态 |
|------|------|------|------|
| **A** | 地基 | Attach、Bus、工作台 Chat；**Activity Snapshot v0** | 完成 |
| **B** | 协同+记忆 | 证据、多 Agent；**纠错/采纳 → L-project**；Knowledge 页 | 未开始 |
| **C** | 生态 | MCP；知识本机导入导出 | 未开始 |
| **D** | 深度聪明 | 向量、手册、主动洞察、L-user 偏好深化 | 按需 |

**架构面：** Bus 可操作对象；上下文进池；可溯源；采集零阻塞；可关 AI。  
**交互面：** 工作台达 Cursor 级日常可用。  
**成长面：** 换会话仍懂本工程；纠错后同类误判下降；Knowledge 可删。

---

## 9. MVP 优先能力清单

| 优先级 | 能力 | 类型 |
|--------|------|------|
| P0 | AI 工作台 UX（Chat/芯片/Settings/Trace） | Experience |
| P0 | Add to AI Chat 汇入工作台 | Experience |
| P0 | Capability Bus + 样例 caps | 架构 |
| P0 | **Activity Snapshot v0** 自动注入 | 上下文 |
| P0 | 审计 | 工程 |
| P1 | **Remember / 纠正并沉淀** + Knowledge 列表 | 知识 |
| P1 | 证据卡片；Plan；多专科 | 协同 |
| P1 | 声明式黄金用例 | 场景 |
| P2 | 主动洞察；MCP | 洞察/生态 |
| P3 | 向量库、手册、跨工程偏好 | 知识深化 |

---

## 10. 关键决策表

| 决策 | 选择 |
|------|------|
| 产品定位 | Native 底座 + Cursor 级工作台 + **越用越聪明的知识层** |
| 上下文 | 活动快照自动注入 + 一切可投喂 |
| 知识 | L-session…L-domain；本地/工程优先；可浏览可删 |
| 变聪明 | 采纳/纠错/剧本沉淀 |
| 交互归属 | 长对话/配置/知识管理在 `ai-agent` |
| 改造策略 | 最小改动；旁路事件与存储 |
| AI 角色 | 协作者；证据+置信度；可关可忘 |
| 编排 | OpenAI Agents SDK |
| 采集 | AI 与知识写入均异步旁路 |
| 隐私 | 默认不上云 |
| 文档 | 架构中文；代码英文 |

---

## 11. 文档与代码索引

| 资源 | 说明 |
|------|------|
| [`ai_implementation_plan.md`](ai_implementation_plan.md) | 分阶段勾选 |
| [`aiagent.md`](aiagent.md) | 工作台规格 |
| [`插件方案.md`](插件方案.md) | ZMQ / sin SDK |
| [`Plugin_Domain_Suites.md`](Plugin_Domain_Suites.md) | 领域套件 |
| `plugins/_shared/capability_bus.py` / `ai_attach.py` | Bus / Attach |
| `plugins/ai-agent/` | 工作台 |
| `sdk/sin/ai.py` | `sin.ai.attach` |

---

## 12. 结语

> **底层 Native；工作台一等公民；始终知道用户在干啥；专业与个人经验沉淀在本机/本工程 — 越用越聪明、越用越懂用户。**  
> 采集不被挡；人永远能关掉、忘掉、回退。

按实施计划：先 Snapshot v0 + 工作台，再 Remember/纠错闭环，最后向量与洞察。
