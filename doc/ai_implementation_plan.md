# openbus AI Native — 实施计划

> 依据：[`ai.md`](ai.md)（**AI Native 产品总纲**）  
> 状态：Phase A **已完成**（2026-09-20）；工作台壳 **A+B 已落地**（Edge 窗内嵌 + 浏览器兜底，无 Qt WebEngine；Tauri 延后）  
> 硬约束：最小改动主进程/插件框架；AI 旁路不阻塞采集；AI 是协作者。  
> **交互**：`ai-agent` 做成 Cursor 级工作台（Chat + Settings + Tools）；全软件只做投喂入口。

---

## 总览

| Phase | Native + 工作台目标 | 状态 |
|-------|---------------------|------|
| **A** | Bus + Attach + 工作台；**Activity Snapshot v0** | 完成 |
| **B** | 证据、Plan、多专科；**Remember/纠错 → Knowledge** | 未开始 |
| **C** | MCP；知识本机包导入导出 | 未开始 |
| **D** | 向量、洞察、跨工程偏好 | 按需 |

---

## Phase A — 拆解

### A0. 工作台 UX（P0）
- [x] Chat + 芯片 + Tool Trace 可折叠
- [x] Provider/Policy 可收纳
- [x] 发送时 resolve 附件 + **注入 Activity Snapshot 摘要**
- [x] **壳 A+B**：stdlib bridge + `webui/dist`；Edge 窗内嵌（B）/ 浏览器（A）；无 Qt WebEngine；Tauri 延后

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

## Phase C / D

见 `ai.md` §8–9。

---

## 发布前检查

架构可操作？可溯源？采集零阻塞？可回退？  
工作台像 Cursor 能天天用？  
**成长：** 换会话仍懂本工程？纠错后更准？记忆可删？  
