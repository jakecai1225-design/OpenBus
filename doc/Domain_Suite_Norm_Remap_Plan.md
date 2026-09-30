# Domain Suite · 插件开发规范改造计划

> **依据：** [插件开发规范.md](插件开发规范.md)（尤其 **§3 四件套**、§2–4、§6、§9、§11–13）  
> **参考实现：** `canopen-suite` / `dbc-studio` / `eds-studio` / `uds-suite` / `obd-suite` / `j1939-suite`（均已 VS Code 四件套）  
> **产品面：** `domainplugins.h` 白名单内领域套件；不含 `ai-agent`（平台 Agent，另轨）、不含 `_retired`  
> **门禁：** 每个套件需求须 **PDCA × 2**（`tmp/pdca/<suite>-norm/`）  
> **修订：** 2026-09-30 · 强制四件套写入规范；UDS/OBD/J1939 壳层已对齐

---

## 1. 目标

把其余领域套件拉齐到同一套「应用级」工作台体验：

1. **壳层一致（强制）** — VS Code 四件套：Activity · Side Bar · Editor Tabs · OUTPUT + Context Next（`suite_chrome` + `suite_tabs` + `suite_ui`）  
2. **导航克制** — Activity ≤ 5；侧栏一叶一事；文件进 File；日志不是活动页  
3. **页间打通** — `set_focus` / `on_focus`；搜索；右键 / Related；`goto_*_target`  
4. **页面配方** — `tool_strip` / `empty_state` / density Token（共享 `suite_ui`）；无每页连接条、无 Caption 墙  
5. **验证可热加载** — 改 `plugins/<id>/` → 重激活；关键契约单测钉住

**非目标（本计划外）：** 协议深度功能对标竞品表的全部 P0 行（见 `Domain_Suite_Competitive_Requirements.md`）；本计划只做**规范合规壳层 + 互操作骨架**，业务加深按各套件后续 feature PDCA。

---

## 2. 现状快照（相对规范）

| 套件 | Activity | 四件套 §3 | Tab 驻留 §13.1 | `run_action` + Next | focus / goto | `_ui` / `suite_ui` | File | 判定 |
|------|----------|-----------|----------------|---------------------|--------------|--------------------|------|------|
| **canopen-suite** | 4 | ✅ | ✅ `_open_tabs` | ✅ | ✅ OD/EDS | ✅ | ✅ | **参考 A** |
| **dbc-studio** | 4 | ✅ | ✅ | ✅ | ✅ Msg/VT/Attr | ✅ | ✅ | **参考 B** |
| **uds-suite** | 5 | ✅ | ✅ `suite_tabs` | ✅ | 会话 focus | ✅ | ✅ | **参考 C** |
| **eds-studio** | 3 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | **Wave 1 CLOSED** |
| **obd-suite** | 3 | ✅ | ✅ | ✅ | ✅ Mode/PID | ✅ | ✅ Export | **Wave 2 CLOSED** |
| **j1939-suite** | 4 | ✅ | ✅ | ✅ | ✅ PGN focus | ✅ | ✅ DBC File | **Wave 2 CLOSED** |
| **autosar-suite** | **6** 超标 | ❌ 半套 | PARTIAL 标题 chrome | ❌ | PARTIAL `goto_editor` | 弱 | ✅ | **Wave 1 待办** |
| **ethercat-suite** | **7** 超标 | ❌ 半套 | PARTIAL（`set_editor_title` 风险） | ❌ | ❌ | ❌ | ❌ | **Wave 2 待办** |

### 共性缺口（共享波次要解决）

| ID | 缺口 | 规范条款 | 对策 |
|----|------|----------|------|
| G0 | 半套壳（缺 Side Bar / Tabs / OUTPUT） | §3 / §13.8 | 勾 §3.3；R1 Accept 四件套全绿 |
| G1 | Activity > 5 或 Log 当活动 | §2.4 / §13.5 | 合并工作区；Log → OUTPUT only |
| G2 | 无 `_open_tabs` / 无 `_on_workbench_page` 导致 Tab 被清 | §13.1 | 拷贝参考壳；挂 workbench hook |
| G3 | 无 `run_action` + Context Next | §6.2 / §6.4 | Session/Document `next_hint` + 状态栏按钮 |
| G4 | 页间无 focus / 无搜索 / 无右键过桥 | §13.2 | `set_focus`+`on_focus`；顶栏 filter；Related |
| G5 | 无 / 弱 density Token | §4.1 | `pages/_ui.py` → `from _shared.suite_ui import *` |
| G6 | 文件仍在侧栏或无 File 菜单 | §7 | File 管 Open/Save/Recent；侧栏只编辑叶子 |

---

## 3. 改造原则（执行纪律）

1. **先壳后叶** — 每个套件先合 **§3 四件套**（Side Bar + Tabs + OUTPUT）+ File / Next / `run_action`，再补页间互操作与页面配方。  
2. **照抄参考，禁止再发明第三种 Tab** — 唯一模板：`suite_tabs.mount_editor_tabs` + `_open_tabs` + `_on_workbench_page`（见参考壳）。  
3. **无半套壳例外** — 单叶 Activity 也要 Tab；禁止仅 `set_editor_title`。  
4. **PDCA × 2** — `tmp/pdca/<suite>-norm/`；Round 1 = 四件套全绿；Round 2 = 互操作 + 页面配方 + 契约测试。  
5. **产品面不动杂烩** — 不把 `_retired` 能力塞回市场；有价值片段只进真实领域套件。  
6. **英文 UI**；验证靠 live-source，不打 `.opk`；`_ui` 优先 re-export `suite_ui`。

---

## 4. 波次与顺序

```
Wave 0（共享）✅
  └─ 规范 §3 四件套 + suite_tabs / suite_ui + Acceptance §8
Wave 1（文件型）
  └─ eds-studio ✅ → autosar-suite（待办：压 Activity + 四件套）
Wave 2（在线 / 扫描型）
  └─ obd-suite ✅ → j1939-suite ✅ → ethercat-suite（待办）
Wave 3（收口）
  └─ 跨套件契约抽样 + Rollout 状态 + 规范回写
```

**不排进本计划：** `ai-agent`（另轨）；已对齐套件仅做回归与互操作加深。

---

## 5. Wave 0 — 共享准备

| 项 | 交付 |
|----|------|
| 壳层检查表 | 本文 §8 Acceptance 打成 checklist，贴进各 `R1-P-plan.md` |
| 参考指针 | 规范已指向 canopen/dbc；可选在 `_shared/suite_chrome.py` 文档串注释「如何挂 `_on_workbench_page`」 |
| 测试模板 | 拷贝 `dbc-studio/tests/test_shell_routes.py` 骨架 → 各套件 `tests/test_shell_routes.py`（Activity 键、Tab 符号、无 CJK 抽样） |
| 不做 | 暂不抽「巨型 BaseAppShell」基类（风险高）；先复制再收敛 |

---

## 6. 各套件工作包

### 6.1 `eds-studio`（Wave 1 · 优先）

**金路径：** Open EDS → 编辑 OD → PDO Map → Validate → Export  

| Round | 内容 |
|-------|------|
| **R1 壳层** | Activity 压到 ≤4（建议：Edit / Analyze / Library / Deliver）；叶子进侧栏；**File** 菜单（New/Open/Save/Recent）；`_open_tabs` 驻留；`_on_workbench_page`；`run_action` + Document `next_hint`；`vscode_theme.apply`；补齐 `_ui` Token |
| **R2 互操作** | `set_focus(index, sub)` + `on_focus`；Editor ↔ PDO Map ↔ Validate 搜索/右键/`goto_*`；空态 CTA；页面 `tool_strip`；契约测试 |

**对标参考：** DBC Studio（同为文件工作室）。  
**风险：** 与 `canopen-suite` EDS 能力重叠——文案区分「文件层 EDS Studio」vs「在线 CANopen」，侧栏不混 Live。

---

### 6.2 `autosar-suite`（Wave 1）

**金路径：** Open/Import ARXML → BSW/COM 配置 → Validate → Export  

| Round | 内容 |
|-------|------|
| **R1 壳层** | Activity ≤5；**§3 四件套全绿**（Side Bar + `_open_tabs` + OUTPUT）；`run_action` + Context Next；`_ui` → `suite_ui` |
| **R2 互操作** | 强化已有 `goto_editor_target`；PDU/Signal/BSW 节点 focus + `on_focus`；BSW ↔ Editor ↔ Validate 搜索/右键；定义类页展示 Default/Type（若有属性面）；空态 CTA |

**风险：** 页多（~20）；R1 只改壳与 IA，禁止顺手重写全部业务页。

---

### 6.3 `obd-suite`（Wave 2 · **CLOSED**）

**金路径：** Setup 连接 → Scanner 读 PID → Readiness →（报告进 OUTPUT/导出）  

| Round | 内容 |
|-------|------|
| **R1 壳层** | ✅ `suite_chrome` + Side Bar + `suite_tabs` + OUTPUT；无 Log Activity；`suite_ui` |
| **R2** | ✅ Session focus（Mode/PID）；空态 CTA；连接只在 Setup |

---

### 6.4 `j1939-suite`（Wave 2 · **CLOSED**）

**金路径：** Live 监视 → Transport(TP) → Diagnostics(DM) → Network  

| Round | 内容 |
|-------|------|
| **R1 壳层** | ✅ 四件套 + File(DBC) + `run_action` / Next + `suite_ui` |
| **R2** | ✅ PGN/DM focus；空态；禁止每页复制总线条 |

---

### 6.5 `ethercat-suite`（Wave 2 · 待办）

**金路径：** ESI/拓扑 → PDO → CoE →（DC/Frames 高级）→ Setup  

| Round | 内容 |
|-------|------|
| **R1 壳层** | Activity 压到 ≤5；**勾满 §3.3 四件套**；消灭 `set_editor_title` 清 Tab；File 管 ESI；`run_action` + Next；`_ui` → `suite_ui` |
| **R2** | Slave/OD focus；Topology ↔ PDO ↔ CoE 互跳；空态 CTA |

---

### 6.6 已合规套件（维护轨）

| 套件 | 动作 |
|------|------|
| uds-suite | 回归；可选后续 `uds-suite-tabs`（Diagnose 多开） |
| dbc-studio | 收口 Attributes/互操作缺陷；密度 PDCA 已有则续 Round |
| canopen-suite | 维持参考；新功能继续 PDCA×2 |

---

## 7. 每套件 PDCA 模板（强制）

目录：`tmp/pdca/<suite>-norm/`

| 文件 | Round 1 | Round 2 |
|------|---------|---------|
| `STATUS.md` | 壳层进行中 | 互操作进行中 → CLOSED |
| `R1-P-plan.md` | IA 目标 Activity/叶子、验收、OOS | — |
| `R1-D-do.md` | app_shell / session / File / Tab / Next | — |
| `R1-C-*.md` | 路由测试 + UX 对照规范 + Accept | — |
| `R1-A-act.md` | 喂 R2 | — |
| `R2-P…A` | — | focus/搜索/右键/empty_state/tool_strip/契约 |

**验收口诀（套用规范 §13.2）：** 选中一次，相关页认得；搜得到、跳得回、改得完；Tab 关不光、点侧栏不丢条。

---

## 8. Acceptance（跨套件统一清单）

壳层（R1 必须全绿）— **VS Code 四件套**：

- [ ] `suite_chrome.build_workbench` + `side_bar_enabled=True` + `vscode_theme` / `suite_ui`
- [ ] Activity ≤ 5；无 Log 活动页；每 Activity 有 Side Bar 叶子
- [ ] `_open_tabs` + `suite_tabs.mount_editor_tabs` + `_on_workbench_page`；关光 reopen 默认叶
- [ ] OUTPUT 底栏默认可见（Ctrl+J）；非活动页
- [ ] File 菜单拥有文档 Open/Save/Recent（若该域有文件）
- [ ] `run_action` + 状态栏 Context Next，阶段可推进
- [ ] 连接/总线仅 Setup 或薄状态，无每页全宽条
- [ ] `tests/test_shell_routes.py`（或等价）PASS；源码无 CJK

互操作（R2 必须全绿）：

- [ ] `set_focus` + `on_focus`（或 session 等价）
- [ ] ≥1 组姐妹页：`goto_*_target` + 右键/Related
- [ ] 主列表页有 Search / `inline_filter`
- [ ] 关键空态带 CTA；页面 `tool_strip` / Token，无 Caption 墙
- [ ] 人工金路径点击清单写入 `R2-C-test.md`

---

## 9. 风险与依赖

| 风险 | 缓解 |
|------|------|
| AUTOSAR / EtherCAT 页过多导致范围膨胀 | R1 只动 `app_shell` + sidebar + session；页面只改 chrome 配方 |
| EDS Studio vs CANopen 用户混淆 | 文案与入口隔离；规范 §2.4 |
| 复制 Tab 代码漂移 | Wave 0 检查表 + 契约测试钉符号名 |
| OBD / J1939 半套壳 | ✅ 已对齐四件套；剩余 Wave：autosar / ethercat |
| 市场/白名单误改 | 本计划**不改** `domainplugins.h`；若改名另开 13.3 三处同步任务 |

---

## 10. 建议排期（里程碑）

| 里程碑 | 内容 | 出口 |
|--------|------|------|
| M0 | Wave 0 检查表 + 测试模板 | 各套件可开 `*-norm` PDCA |
| M1 | eds-studio CLOSED | 第二个「文件工作室」合规 |
| M2 | autosar-suite CLOSED | 最复杂套件壳层合规 |
| M3 | obd + j1939 CLOSED | 扫描类合规 |
| M4 | ethercat CLOSED + Wave 3 收口 | 全领域套件规范门禁通过 |

---

## 11. 与旧文档关系

| 文档 | 关系 |
|------|------|
| [Domain_Suite_Rollout_Plan.md](Domain_Suite_Rollout_Plan.md) | P0 视觉已基本完成；**本文件接替「规范合规」轨**（Tab/互操作/IA） |
| [Plugin_Domain_Suites.md](Plugin_Domain_Suites.md) | 套件地图仍有效；杂烩已退役 |
| [Suite_UI_UX_Design_System.md](Suite_UI_UX_Design_System.md) | 视觉细则；与本计划同时遵守 |
| [插件开发规范.md](插件开发规范.md) | **总准则**；冲突时以规范 §13 更新为准 |

---

*下一步：确认 Wave 1 先做 `eds-studio` 还是 `autosar-suite` 后，开 `tmp/pdca/<suite>-norm/R1-P-plan.md` 开工。*
