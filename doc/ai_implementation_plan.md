# openbus AI Native — 实施计划

> 依据：[`ai.md`](ai.md)（**AI Native 产品总纲**）  
> 状态：Phase A **已完成**；工作台壳升级为 **WebView2 + TS UI + Node AI-SDK sidecar**（2026-09-27）；无 Qt WebEngine；Tauri 延后  
> 硬约束：最小改动主进程/插件框架；AI 旁路不阻塞采集；AI 是协作者。  
> **交互**：Cursor 级工作台（Chat + Settings + HITL Approve）；全软件只做投喂入口。

---

## 总览

| Phase | Native + 工作台目标 | 状态 |
|-------|---------------------|------|
| **A** | Bus + Attach + 工作台；**Activity Snapshot v0** | 完成 |
| **A+** | 恢复 `tools/`；真 WebView2；Vite/React webui；Node AI-SDK sidecar；`/api/tools` | 完成 |
| **B** | 证据、Plan、多专科；**Remember/纠错 → Knowledge**（可用 Mastra memory） | 未开始 |
| **C** | MCP Server 暴露同一 ToolRegistry；知识本机包导入导出 | 未开始 |
| **D** | 向量、洞察、跨工程偏好 | 按需 |

---

## Phase A — 拆解

### A0. 工作台 UX（P0）
- [x] Chat + 芯片 + Tool Trace 可折叠
- [x] Provider/Policy 可收纳
- [x] 发送时 resolve 附件 + **注入 Activity Snapshot 摘要**
- [x] **壳 A+B**：stdlib bridge + `webui/dist`；WebView2/Edge 窗内嵌（B）/ 浏览器（A）；无 Qt WebEngine；Tauri 延后
- [x] **A+**：`tools/` 恢复；Vite+TS webui；HITL Approve UI；`agent-ts` AI SDK sidecar；Python Orchestrator fallback

### A1. Capability Bus
- [x] `capability_bus.py`
- [x] 套件 caps + Agent 合并（收口）

### A2. Attach → 工作台
- [x] `ai_attach` + `sin.ai`
- [x] Trace Add to AI Chat
- [x] 至少一处套件投喂（CANopen EDS → Add to AI Chat）

### A2b. Activity Snapshot v0
- [x] 快照字段：工程路径、激活插件/页、选区摘要、测量 on/off
- [x] 旁路更新（防抖）；可关
- [x] 写入系统提示短块（预算内）

### Phase A 验收
配模型 →（自动带上「我在哪」）→ 投喂 Trace → 提问 → 见工具轨迹；无 AI 时经典流程完整。

---

## Phase B — 越用越聪明（最小闭环）

- [ ] 工作台按钮：采纳 / 纠正并沉淀 / 拒绝  
- [ ] L-project 案例卡落盘（工程目录或 state_store）  
- [ ] Knowledge 页：列表、删除、导出  
- [ ] `knowledge.search` 在提问前检索命中摘要  
- [ ] 证据卡片可点回 Trace/DBC  

**Knowledge 落点（与 Mastra 对齐）**

| 组件 | 位置 | 说明 |
|------|------|------|
| 案例卡 / 纠错 | `~/.openbus/ai-agent/knowledge/` 或工程 `.openbus/knowledge/` | JSONL；UI 列表/删除/导出 |
| 检索工具 | Python `tools` → `knowledge_search` | 提问前注入 system 短块 |
| Mastra memory | `agent-ts` 可选 `@mastra/memory` | 会话级；工程知识仍以本机文件为准（可审计、可删） |

## Phase C — MCP 外向

- [ ] `plugins/ai-agent/mcp/server.py`（或 agent-ts MCP）：`tools/list` + `tools/call` → 同一 `ToolRegistry`
- [ ] 默认 **stdio / 本机 HTTP**，不启用公网 Hosted MCP
- [ ] 只读工具默认暴露；写工具需与 Policy + HITL 一致
- [ ] 验收：Cursor / Claude Desktop 可连接并调用 `frames_stats`

## Phase D

见 `ai.md` §8–9（向量、跨工程偏好 — 按需）。

---

## 运行时栈速查（A+）

```
ChatWindow (PyQt)
  ├─ BridgeServer 127.0.0.1  — settings / tools / approve / webui/dist
  ├─ Node agent-ts (optional) — AI SDK streamText tool-loop → /api/tools/invoke
  └─ WebView2 (pywebview) / Edge --app / system browser
Python tools/ = sole bus executor (SinHost + Policy + Capability Bus)
```

需要 Node ≥ 20：`cd plugins/ai-agent/agent-ts && npm i && npm run build`  
需要 UI 重建：`cd plugins/ai-agent/webui && npm i && npm run build`  
可选：`pip install pywebview`（真 WebView2）

---

## 发布前检查

架构可操作？可溯源？采集零阻塞？可回退？  
工作台像 Cursor 能天天用？  
`.opk` 含 `tools/registry.py` 与 `webui/dist`（勿再被 gitignore）？  
**成长：** 换会话仍懂本工程？纠错后更准？记忆可删？  
