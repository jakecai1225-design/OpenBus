# Plugin Domain Suites — 领域套件策略

> 状态：策略定稿 + 需求收集（2026-09-18）  
> 取代方向：Wave A/B 的「多而细」工具插件 → **少而完整的领域套件**  
> 产品原则：**不以数量取胜；颗粒度要大；一个插件 = 该领域一条龙应用**

相关文档：

- 旧升级轨（保留作历史）：[Plugin_P0_Competitive_Upgrade.md](Plugin_P0_Competitive_Upgrade.md)、[Plugin_Category_B_Upgrade.md](Plugin_Category_B_Upgrade.md)
- 本文件：合并地图、UX 条、迁移、三领域需求（UDS / DBC / CANopen）
- AI Agent 仍独立设计：[aiagent.md](aiagent.md)（消费领域套件的工具面，而不是再加碎片插件）；平台总方案见 [ai.md](ai.md)

---

## 1. 为什么改

| 旧模式问题 | 新模式目标 |
|------------|------------|
| 30+ 独立入口，菜单碎片化 | ~8–12 个领域套件，主菜单干净 |
| 单页 + 表单 =「工具感」 | 多工作区 + 共享会话 =「应用感」 |
| 同领域能力割裂（UDS×4、DBC×5） | 一领域一插件，共享连接/日志/配置 |
| 对标散落在各小工具 | 每个套件有明确竞品与验收清单 |
| 学完一个还要找下一个插件 | 打开即覆盖该领域常用闭环 |

**一句话：** 功能插件要对标「该领域的独立应用」，而不是「一个对话框」。

---

## 2. 套件定义（什么叫够大）

一个领域套件必须同时满足：

1. **一条龙**：覆盖该领域 80% 常用任务（查看 / 编辑 / 执行 / 校验 / 导出），而不是只做其中一步。  
2. **共享会话**：总线连接、请求/响应 ID、会话状态、日志、项目路径在套件内全局共享。  
3. **多工作区 UX**：左侧导航（或等价 IA）切换 Diagnose / Scan / …，不是平行弹多个独立窗。  
4. **对标清晰**：文档写明竞品与「达到 / 不做」边界。  
5. **可持久化**：配置、最近文件、布局、配置文件进 `state_store` / 工程目录。  
6. **可扩展**：协议条目库、配置文件、脚本/序列可加载，而不是写死几个按钮。

不够大的例子（应合并）：单独的 dbc-lint、dbc-diff、uds-scan。  
够大的例子：DBC Studio（看/编/校/存/转）、UDS Suite（诊/扫/批/安）、CANopen Suite（收发/EDS/OD/402）。

---

## 3. 目标套件地图（33 → ~10）

| Suite ID | 产品名 | 吸收的旧插件 | 对标 |
|----------|--------|--------------|------|
| `uds-suite` | **UDS Suite** | uds-diagnostic, uds-scan, uds-batch, uds-security-audit | Softing DTS / CANoe Diag / TSMaster UDS |
| `dbc-studio` | **DBC Studio** | dbc-lint, dbc-diff, dbc-codegen, dbc-exporter, dbc-merge + 主进程 DBC 能力对齐 | Vector CANdb++ |
| `canopen-suite` | **CANopen Suite** | canopen-scanner + 新建 EDS/OD/PDO/NMT | CANopen Magic / Peak CANopen |
| `j1939-suite` | **J1939 Suite** | j1939-analyzer（扩展 TP/RQST/DM） | PEAK J1939 / CANoe J1939 |
| `obd-suite` | **OBD Suite** | obd2-scanner（扩展 Mode01–09、冻结帧、报告） | FORScan / Torque Pro |
| `tx-lab` | **TX / Restbus Lab** | can-frame-generator, can-simulator, can-dashboard | CANoe IG / Restbus / Panel |
| `bus-security` | **Bus Security** | can-fuzzer, can-ids, can-stress, e2e-checksum, uds audit 可交叉链到 UDS | Caring Caribou / 内安工具 |
| `protocol-monitors` | **Protocol Hub** | autosar-nm, iso-tp, isobus, nmea2000, gbt27930, xcp | 各协议专用观察器合一入口 |
| `log-analysis` | **Log & Compare** | log-toolkit, frame-compare, trigger-logger, can-quality-report, can-reverse, can-id-scanner | CANalyzer + asammdf lite |
| `bus-utilities` | **Bus Utilities** | can-bit-timing, can-gateway | 计算器 + 网关规则 |
| `autosar-suite` | **AUTOSAR Suite** | 新建（Protocol Hub 只保留 NM 观察；本套件做 COM / ARXML / E2E / SecOC） | CANoe.AUTOSAR / DaVinci / TSMaster COM |
| `ethercat-suite` | **EtherCAT Suite** | 新建 | TwinCAT ESI / EC-Engineer / SOEM 工具链 |
| `ai-agent` | **AI Agent** | （新建，见 aiagent.md） | Majster-AI × MCP |

> 实施顺序建议：**UDS Suite → DBC Studio → CANopen Suite → TX Lab → 其余**。  
> S5 已移除旧插件目录。源码树与 `plugin_tool.py pack-suites` 只保留领域套件；自定义插件仍可用 `pack <dir>` 单独打包。

---

## 4. UX 条（所有套件共用）

### 4.1 信息架构

```
┌────────┬──────────────────────────────────────────────┐
│ Brand  │  Suite title                    [Conn] [?]   │
├────────┼──────────────────────────────────────────────┤
│ Nav    │  Shared strip: IDs / session / bus / status  │
│ • A    ├──────────────────────────────────────────────┤
│ • B    │                                              │
│ • C    │           Active workspace (rich)            │
│        │                                              │
│        ├──────────────────────────────────────────────┤
│        │  Shared activity log / issues / export       │
└────────┴──────────────────────────────────────────────┘
```

- **左栏导航**：工作区名称 + 图标，当前项高亮；快捷键 `Ctrl+1…`。  
- **顶栏共享条**：该领域全局参数（UDS：物理/功能/响应 ID、会话、TesterPresent；DBC：当前库路径；CANopen：Node-ID、EDS）。  
- **底栏活动日志**：统一时间线，可暂停/过滤/导出。  
- **空状态**：每个工作区有「下一步做什么」的引导，不是空白表单。  
- **英文 UI**（代码与字符串）；文档可用中文。

### 4.2 交互质量

- 危险操作（刷写、fuzz、清码、写 OD）二次确认 + 可停止。  
- 长任务：进度、取消、部分结果可导出。  
- 配置文件（profile / EDS / DBC）拖放打开。  
- 窗口最小尺寸按「应用」定（例如 1100×720），可记住几何。

### 4.3 工程集成

- `state_store`：`project/plugins/<suite-id>/`  
- 可选用主机 DBC / 工程路径  
- 单一 `register_command`：`Suite: Open …`；旧命令映射到同一入口 + 初始页参数

---

## 5. 迁移规则

1. **新建** `plugins/<suite-id>/` 为唯一推荐入口。  
2. **旧插件目录已在 S5 删除**。套件是唯一入口；不要再恢复细插件目录。自定义插件用 `plugin_tool.py pack`，市场发布用 `pack-suites`。  
3. **能力搬迁**：逻辑模块迁入套件包内（`pages/`、`core/`）；避免继续加深旧目录。  
4. **测试**：核心协议栈保留/迁移单测（如 UDS profile_seq）。  
5. **市场/打包**：`.opk` 以套件为单位发布。

---

## 6. 需求 — UDS Suite

### 6.1 定位与对标

| 项 | 内容 |
|----|------|
| 产品名 | UDS Suite |
| 插件 ID | `uds-suite` |
| 对标 | Softing DTS Monaco（诊断台）、CANoe Diagnostic Console、TSMaster UDS、ZCANPro 诊断 |
| 吸收 | uds-diagnostic / uds-scan / uds-batch / uds-security-audit |
| 不做（v1） | 完整 OEM 刷写生态、DoIP 网关管理、安全密钥服务器（仅本地算法钩子） |

### 6.2 用户故事（必须覆盖）

1. 工程师连接 CAN，配置请求/响应 ID，进入扩展会话，读 DID / 清 DTC。  
2. 未知总线上扫描诊断地址，得到 ECU 地图并一键填入共享连接条。  
3. 导入 CSV/JSON 测试序列，批量跑通并导出 PASS/FAIL 报告。  
4. 观察 SecurityAccess 种子熵与 NRC（只读审计，不爆破）。  
5. 保存 ECU profile（ID、DID 字典、序列）到工程，下次一键恢复。  
6. 刷写助手：预检 → 34/36/37 → 校验（沿用现有能力，强化进度与中止）。

### 6.3 工作区（v1）

| 工作区 | 能力 | 来源 |
|--------|------|------|
| **Diagnose** | 服务树、DID、DTC、Security、Flash、实时解码 | uds-diagnostic |
| **Scan** | ID 范围探测、服务探测、地图 → Apply to connection | uds-scan |
| **Batch** | CSV/JSON 序列、统计、导出 | uds-batch |
| **Security** | 种子熵、时序、NRC 直方图、报告 | uds-security-audit |
| **Profiles** | ECU profile / 序列文件管理 | profile_seq 扩展 |
| **Log** | 全局诊断日志（可从各页写入） | 新建共享 |

### 6.4 共享会话对象

- `tx_id`, `rx_id`, `func_id`, addressing mode  
- ISO-TP：BS / STmin / padding  
- UDS：P2 / P2*、当前会话、TesterPresent 周期  
- `UdsClient` / `IsotpLayer` 单例（全套件共用，禁止每页各建一套互抢）

### 6.5 验收（v1）

- [x] 单一窗口，六工作区可切换，连接条全局生效  
- [x] Scan「Apply」写回连接条；Diagnose 立即使用新 ID  
- [x] Batch / Security 使用同一 ISO-TP 栈（Scan 临时 IsotpClient 探范围，文档注明）  
- [x] 英文 UI；危险操作确认  
- [x] 旧四个命令仍可用（stub → suite + page）  
- [x] 既有 `profile_seq` / 协议单测绿（`plugins/uds-suite/tests/`）

---

## 7. 需求 — DBC Studio

### 7.1 定位与对标

| 项 | 内容 |
|----|------|
| 产品名 | DBC Studio |
| 插件 ID | `dbc-studio` |
| 对标 | **Vector CANdb++**（主）、canmatrix / cantools（辅助） |
| 吸收 | dbc-lint, dbc-diff, dbc-codegen, dbc-exporter, dbc-merge |
| 与主机关系 | 可打开工程 DBC；保存后通知主机重载（若 API 允许） |
| 不做（v1） | 完整 ARXML 双向、OEM 专有属性编辑器全家桶 |

### 7.2 用户故事

1. 打开 / 新建 DBC，树形浏览 Network → Node → Message → Signal。  
2. 编辑信号起止位、字节序、因子偏移、VAL_、注释；保存回 `.dbc`。  
3. 一键 Lint（规则集 + suppress），问题列表可跳转到信号。  
4. Diff 两个 DBC，HTML/CSV 报告。  
5. Merge 多库，冲突策略可选。  
6. Export 矩阵（CSV/JSON/HTML）与 C codegen。

### 7.3 工作区（v1）

| 工作区 | 能力 |
|--------|------|
| **Editor** | 树 + 属性检视/编辑 + 保存/另存 |
| **Validate** | Lint 规则、SARIF/JSON CI 输出 |
| **Compare** | A/B diff |
| **Merge** | 多文件合并 + 冲突报告 |
| **Export** | 矩阵 / 代码生成 |
| **Library** | 最近文件、工作区 DBC 列表 |

### 7.4 验收（v1）

- [x] 对标 CANdb++ 的「能改能存」最小闭环（非只读查看器）  
- [x] Lint/Diff/Merge/Export 不再需要离开套件  
- [x] 非法编辑有校验提示；保存前可强制 Lint  
- [x] 英文 UI；共享当前文档路径

---

## 8. 需求 — CANopen Suite

### 8.1 定位与对标

| 项 | 内容 |
|----|------|
| 产品名 | CANopen Suite |
| 插件 ID | `canopen-suite` |
| 对标 | CANopen Magic、PEAK CANopen、CANeds |
| 吸收 | canopen-scanner + **新建** EDS/OD/PDO/NMT/402 |
| 不做（v1） | 完整 CiA 402 运动控制示教器、LSS 全矩阵自动化产线 |

### 8.2 用户故事

1. 扫描网络节点（Heartbeat / Identity 0x1018），选中节点。  
2. 加载 EDS/DCF，浏览对象字典；SDO 读/写条目。  
3. 内置 **CiA 301** 标准对象库；可选 **CiA 402** 驱动剖面条目。  
4. NMT 启停、PDO 映射查看（v1 以查看+基础映射编辑为主）。  
5. EDS 查看与基础编辑（增删 OD 条目、导出）。  
6. 总线监视：COB-ID 分类（NMT/EMCY/PDO/SDO/HB）时间线。

### 8.3 工作区（v1）

| 工作区 | 能力 |
|--------|------|
| **Network** | 扫描、节点表、NMT |
| **Monitor** | COB 分类监视与过滤 |
| **Object Dictionary** | EDS 树 + SDO R/W |
| **Profiles** | 301 / 402 条目库浏览器 |
| **EDS Editor** | 基础编辑 + 导出 |
| **Log** | 共享活动日志 |

### 8.4 验收（v1）

- [x] 扫描 → 选节点 → SDO 读 0x1018 一条龙  
- [x] 无 EDS 时 OD 树与 SDO 联动  
- [x] 301/402 库可搜索插入到编辑器  
- [x] 英文 UI；写 SDO 确认

---

## 9. 实施计划

| 阶段 | 内容 | 状态 |
|------|------|------|
| S0 | 本策略 + UDS/DBC/CANopen 需求定稿 | **done（本文）** |
| S1 | **UDS Suite** 壳 + 共享会话 + 六工作区落地；旧 UDS 插件 stub | **done（2026-09-18）** |
| S2 | **DBC Studio** 编辑器闭环 + Validate/Compare/Merge/Export | **done（2026-09-18）** |
| S3 | **CANopen Suite** Network/Monitor/OD/EDS/Profiles | **done（2026-09-18）** |
| S4 | TX Lab / Bus Security / Protocol Hub / Log Analysis / Bus Utilities / J1939 / OBD | **done（2026-09-18）** |
| S5 | 清理 deprecated 目录；市场只发套件 | **done（2026-09-18）** |

---

## 10. 决策日志

- 2026-09-18：废弃「以插件数量铺功能」策略；改为领域套件；首发 UDS → DBC → CANopen。  
- Wave A/B 的 `_shared` 壳与状态机保留，作为套件基建。  
- 细粒度插件代码是迁移原料，不是最终产品形态。  
- 2026-09-18：**UDS Suite v2.0.0** 落地于 `plugins/uds-suite/`（Diagnose/Scan/Batch/Security/Profiles/Log + SharedSession）；旧四插件改为 stub（`goto.json` + `sin.commands.execute("udsSuite.open")`）。Scan 范围探测仍用临时 `_shared.IsotpClient`，避免改写共享栈 ID。
- 2026-09-18：**CANopen Suite v2.0.0** 落地于 `plugins/canopen-suite/`（Network/Monitor/Object Dictionary/Profiles/EDS Editor/Log + SharedSession：Node-ID、EDS OD、expedited SDO、NMT）；`canopen-scanner` 改为 stub → Network；`tests/test_eds_parse.py` 覆盖 EDS 解析与 AST/CJK 检查。
- 2026-09-18：**DBC Studio v2.0.0** 落地于 `plugins/dbc-studio/`（Editor/Validate/Compare/Merge/Export/Library + DbcDocument）；旧五插件（lint/diff/merge/exporter/codegen）改为 stub；`tests/test_serialize_roundtrip.py` 覆盖序列化/Lint/Merge。
- 2026-09-18：**Bus Security v2.0.0** 落地于 `plugins/bus-security/`（Fuzzer/IDS/Stress/E2E/Log + SharedSession：rate defaults、Stop All）；旧四插件（can-fuzzer / can-ids / can-stress / e2e-checksum）改为 stub → 对应工作区；危险操作二次确认；`PLUGIN_ID=bus-security` 持久化。
- 2026-09-18：**TX Lab v2.0.0** 落地于 `plugins/tx-lab/`（Generator / Restbus / Dashboard / Log + SharedSession：可选 DBC、总线状态、Start/Stop all TX）；旧三插件（can-frame-generator / can-simulator / can-dashboard）改为 stub → 对应工作区；`tests/test_import_smoke.py` 覆盖 AST 导入与零 CJK。
- 2026-09-18：**Log Analysis v2.0.0**（产品名 Log & Compare）落地于 `plugins/log-analysis/`（Toolkit / Compare / Trigger / Quality / Reverse / ID Scan / Log + SharedSession：working folder / last paths）；旧六插件（log-toolkit / frame-compare / trigger-logger / can-quality-report / can-reverse / can-id-scanner）改为 stub → 对应工作区；`tests/test_import_smoke.py` 覆盖 AST 导入与零 CJK。
- 2026-09-18：**Protocol Hub v2.0.0** 落地于 `plugins/protocol-hub/`（NM / ISO-TP / ISOBUS / NMEA2000 / GBT27930 / XCP / Log）；旧六协议监视器（autosar-nm-monitor / iso-tp-monitor / isobus-monitor / nmea2000-decoder / gbt27930-monitor / xcp-monitor）改为 stub → 对应工作区。
- 2026-09-18：**Bus Utilities v2.0.0** 落地于 `plugins/bus-utilities/`（Bit Timing / Gateway / Log）；`can-bit-timing` / `can-gateway` 改为 stub。
- 2026-09-18：**J1939 Suite v2.0.0** 落地于 `plugins/j1939-suite/`（Analyzer / Log）；`j1939-analyzer` 改为 stub → Analyzer。
- 2026-09-18：**OBD Suite v2.0.0** 落地于 `plugins/obd-suite/`（Scanner / Log）；`obd2-scanner` 改为 stub → Scanner。
- 2026-09-18：**S4 收口**：领域套件地图内除 AI Agent 外的全部套件均有可打开壳 + 旧插件 stub；S5 仅剩目录清理与市场打包策略。
- 2026-09-18：**S5**：删除已吸收的细插件目录（UDS×4、DBC×5、CANopen scanner、TX×3、Bus Security×4、Protocol Hub×6、Log×6、Bus Utilities×2、J1939 analyzer、OBD scanner）。协议自测迁到 `plugins/uds-suite/tests/test_proto.py`。市场打包命令为 `python scripts/plugin_tool.py pack-suites`（只打 10 个套件 `.opk`）。`pack <dir>` 仍可用于自定义插件。AI Agent 仍见 aiagent.md，未纳入本批。
- 2026-09-18：**本地市场索引**：`python scripts/make_market.py` 生成 `build/bin/market/market.json`（drivers 仍引用远程包 URL；plugins 仅领域套件 + AI Agent）。应用 `market.url` 置空后自动加载 exe 旁本地索引，不再展示 sin.org.cn 上的 37 个旧细插件。
- 2026-09-20：**AI Agent Phase 2（v0.2.0）**：Policy 分级 + HITL 审批；`frames_send` / `uds_read_did` / `obd_read_pid`；工件 `tx_build_cyclic` / `uds_build_sequence`；领域套件地图本身 S0–S5 已收口，后续增强见 aiagent.md Phase 3+。
- 2026-09-20：**协议描述文件编辑器**：DBC Studio 已完整；EDS 加强 FileInfo 往返 / Validate / Save；AUTOSAR System 升为 ARXML COM 子集编辑器（Tree/Validate/Export DBC）；EtherCAT ESI 升为可编辑可保存。LDF/FIBEX/ODX 暂缓。
