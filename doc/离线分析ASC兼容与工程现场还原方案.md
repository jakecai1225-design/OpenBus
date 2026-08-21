# 离线分析 ASC 兼容与工程现场还原方案

> 状态：已实施（2026-08-21，实施清单 11 项全部完成，回归 8/8 套件全绿，见 doc/测试报告.md §9）
> 日期：2026-08-21
> 来源：用户实测反馈两条

## 一、背景与问题

用户实测反馈：

- **问题 1**：CAN Flow 的离线分析不支持实时录制的 .asc 格式 CAN 报文数据，只支持 blf 格式。
- **问题 2**：工程管理没有完全记录工程配置与数据信息；关闭软件后重新打开加载工程，无法完全还原当时的现场（离线分析文件、信号等）。

## 二、问题 1：根因分析

### 2.1 现象边界（调研结论）

| 场景 | 结果 |
| --- | --- |
| 本软件录制的 ASC（含 CAN FD 64B 标准帧） | **正常**。真实 C++ 单测验证：用户 8-12 录制的 `2026-08-12_12-25-37.asc` 3147 帧、61.68s 全部解析成功 |
| 第三方工具（CANoe / ZCANPRO 等）导出的 ASC | **失败**：解析 0 帧 |
| BLF（任意来源） | 正常 |

用户确认：失败文件为第三方工具录制，现象为"解析失败 / 帧数为 0"。

### 2.2 根因：两套 ASC 解析实现能力不一致

代码库中存在**两套独立的 ASC 解析器**：

| | `src/core/canfileio/asc.cpp` `AscReader` | `src/core/file_import/asc_importer.cpp` `AscImporter` |
| --- | --- | --- |
| 使用入口 | 离线分析页、Flow 开始、回放页、Trace 拖放、"打开文件" | 菜单"文件 → 导入日志" |
| 解析方式 | 逐字符扫描，**只认本软件 AscWriter 写出的格式** | token 化，支持多格式变体 |
| CAN/CANFD 关键字格式 | **不支持** | 支持 |
| 格式 B（ID 在方向前） | 不支持 | 支持 |
| 头部 `internal events logged` / `Begin TriggerBlock` | 不跳过（行被当帧行解析失败丢弃） | 跳过 |

### 2.3 复现证据（test/resources 现成样本）

**样本 1** `can_20250526210818.asc`（CANoe 7.0 格式）：

```
date Mon May 26 09:06:17 PM 2025
base hex timestamps absolute
// version 7.0.0
0.000000 CANFD 2 1b1 Rx 1 0 d 15 64 08 80 ...
```

AscReader 逐 token 解析：时间戳 OK → 通道号 token = `"CANFD"` → `toUInt()` 失败 → **整行 continue 跳过** → 全文件 0 帧。

**样本 2** `test_L035.asc`（CANoe 15.7 格式）：

```
date Fri Jul 31 09:14:53.275 am 2026
base hex  timestamps absolute
internal events logged
// version 15.7.0
Begin TriggerBlock Fri Jul 31 09:14:53.275 am 2026
   0.001005 CANFD   1 Rx        30b   1 1 f 64 00 ... 0a 68 91   374544  678   307000 ...
```

同样在第 2 个 token `"CANFD"` 上全行丢弃 → 0 帧。且帧行尾部带附加列（持续时间/周期等），AscReader 的固定位置扫描也无法处理。

**而 AscImporter 对两个样本都能正确解析**（CANFD 关键字分支 + flags 列跳过 + 读满 dataLen 停止）。

### 2.4 附带说明（非本次根因）

8-20 提交 `8845b88` 已修复历史 bug：AscWriter 扩展帧 ID 曾写为 `x1ABCDEF`（x 前缀），导致本软件自写自读失败。用户样本为标准帧不受影响，此 bug 与本次反馈无关，仅作记录。

## 三、问题 1：修复方案

### 3.1 核心思路

**将 AscImporter 的健壮 token 解析逻辑移植进 `AscReader::readAll`**，使离线分析、Flow 开始、回放、Trace 拖放等所有入口统一获得第三方格式能力。

### 3.2 具体改动（src/core/canfileio/asc.cpp）

1. **头部跳过升级**：在现有 `startsWith(date/base/no internal///)` 基础上，改用正则跳过行 `^(date|base|internal|begin|end|//|;)`（覆盖 `internal events logged`、`Begin/End TriggerBlock`、版本注释）。
2. **行解析替换为 token 化**（移植 AscImporter::parseLine 的完整分支矩阵）：
   - 格式 A：`<time> [CAN|CANFD] <ch> <Dir> [FD[x]] <id> ...`
   - 格式 B：`<time> [CAN|CANFD] <ch> <id> <Dir> ...`
   - ID 解析：支持 x 后缀（扩展帧）与 0x 前缀
   - CANFD 关键字分支：跳过 flags×2、`d`/`f` 标记、dlc 码（十六进制）、dataLen（十进制）、读满 dataLen 字节停止（自动忽略行尾附加列）
   - 经典/FD 无关键字分支：dlc（FD 为实际长度）、可选 `d` 标记、逐字节读到非数据字节为止
3. **保留现有行为**：
   - `baseTime` 时间戳归一化（首帧为基准减法——对 CANoe 相对时间戳无副作用）
   - 接口不变（`open/readAll/close`、返回帧数语义）
   - 本软件格式 A 的行尾 `BRS`/`ESI` 检测（仅在无 CAN 关键字的 FD 分支保留）
4. **AscImporter 本次不动**：其行为已正确且无单测覆盖，避免无保护的回归。在两处源码加注释标注"解析逻辑同源，后续统一为共享实现"。

### 3.3 不做的事

- 不引入新的格式探测/自动嗅探层——按 token 内容自然分流即可。
- 不改 BLF/CSV/PCAP/TRC 读取器。

## 四、问题 2：缺口分析

`mainwindow_project.cpp` 的 `captureProjectState` / `applyProjectState` 当前缺口：

| # | 缺口 | 现状代码 |
| --- | --- | --- |
| 1 | **离线分析文件列表完全不保存** | captureProjectState 无任何离线文件捕获 |
| 2 | Trace 只保存 trace1 | `m_traceInstances.value("trace1")` 硬编码；多实例全部丢失 |
| 3 | Graphic 只保存 graphic1 | `m_graphicInstances.value("graphic1")` 硬编码；多实例及其信号列表丢失 |
| 4 | openTabs 保存但不恢复 | capture 保存了标签页顺序，apply 只恢复 activeTab；回放/离线分析/录制/发送页不重建 |
| 5 | recordFiles 不捕获 | ProjectState 有该字段（工程树"录制文件"节点），但运行时从不填充 |

序列化层（projectmanager.cpp）的 `traces`/`graphics` 本就是数组结构、`resources` 相对路径机制齐备——**数据结构层无障碍，纯粹是捕获/恢复逻辑不完整**。

## 五、问题 2：修复方案

### 5.1 数据结构与序列化

- `ProjectState`（projectmanager.h）新增 `QStringList offlineFiles;`
- `stateToJson`：写入 `j["resources"]["offline"]`（相对路径，与 dbc/logs 同风格，工程可移植）
- `jsonToState`：读取 `resources.offline`（旧工程无此键时为空，向后兼容）

### 5.2 捕获端（captureProjectState）

```text
st.offlineFiles = transceiveQuery("offlineFiles")          // 现成查询接口
st.traces     = 遍历 m_traceInstances.keys()（按编号排序）
                 每个存 {id, title="TraceN", filterExpression}   // traceQuery("filterExpression", id)
st.graphics   = 遍历 m_graphicInstances
                 每个存 {id, title="GraphicN", sigList}          // graphicQuery("signalConfigs", w)
```

录制文件捕获：`mainwindow_setup.cpp` 的 `recStopRelay`（recordingStopped 处理）中把 path 追加进 `ProjectManager::currentStateRef().recordFiles`（去重）——工程树"录制文件"节点从此有数据来源。

### 5.3 恢复端（applyProjectState）

1. **关闭残留页**：在现有关闭 Trace/Graphic 标签页之外，增加关闭"回放/离线分析/录制/发送"页（切换工程时旧页残留问题）。
2. **重建页面**：遍历 `st.openTabs`，按标签名映射重建：
   - "Flow" → `onOpenMeasurementSetup()`
   - "回放"/"离线分析"/"录制"/"发送" → 对应 `onOpenXxxTab()`
   - DBC 详情页等复杂页面暂不重建（后续版本）
3. **多实例恢复**（替换现有"只恢复 trace1/graphic1"硬编码）：
   - 遍历 `st.traces` → `createTraceInstance(t.id)` + `setFilterExpression`
   - 遍历 `st.graphics` → `createGraphicInstance(g.id)` + `loadSignalConfigs`（DBC 查补逻辑沿用现有 buildSignalMap 路径）
   - 兜底：状态为空时仍创建默认 trace1/graphic1（现状保持）
4. **离线分析文件恢复**：`transceiveInvoke("addOfflineFiles", st.offlineFiles)`。
   - 顺序依赖：openTabs 重建先调 `onOpenOfflineAnalysisTab()`（壳建页），之后 invoke 才找得到页面。
5. Flow 画布实例注册（现有第 8 步）自然遍历全部 traces/graphics，无需改。

### 5.4 接口新增

- **OfflineAnalysisTab**：新增公开方法 `void addFiles(const QStringList &paths)`（插表 + 入解析队列；`onAddFile` 重构为"弹对话框后调它"）。
- **TransceiveModule::invoke**：新增 action `"addOfflineFiles"`（arg 为路径列表 → 转发给离线分析页 `addFiles`；页面不存在时静默忽略，与模块其他 action 约定一致）。

### 5.5 工程树展示（ProjectPanel）

`refreshList()` 新增"离线分析文件 (N)"分类节点；新增 `extractOfflineFiles(stateJson)` 解析 `resources.offline`（双击预览复用现有 filePreviewRequested）。

## 六、测试方案

### 6.1 自动化（tests/）

- **test_canfileio.cpp** 新增：
  - `readAscV7ThirdParty`：读 `can_20250526210818.asc`，断言帧数 > 0 + 帧自洽不变量
  - `readAscV15ThirdParty`：读 `test_L035.asc`，同上
  - 现有用例回归：`readAsc`（info.asc）、三个 roundtrip（本软件格式闭环不被破坏）
- **test_project.cpp**：`fillState` 增加 offlineFiles；`jsonRoundtrip` / `saveLoadFile` 断言无损往返（相对路径 resolve 后与原绝对路径一致）

### 6.2 手动验收

1. 第三方 ASC → 离线分析页添加 → 帧数列非 0 → Flow 开始 → Trace 出数据
2. 配置现场：2 个 Trace（其一带过滤表达式）+ 2 个 Graphic（带信号）+ 离线分析 2 个文件 + 打开若干标签页 → 保存工程 → 重启软件 → 打开工程 → 逐项核对还原
3. 切换工程（非重启路径）同样核对

### 6.3 文档

`doc/测试报告.md` 追加本轮变更记录。

## 七、实施清单

| # | 文件 | 改动 |
| --- | --- | --- |
| 1 | `src/core/canfileio/asc.cpp` | AscReader::readAll 解析逻辑重写（token 化，移植 AscImporter 分支矩阵） |
| 2 | `src/core/projectmanager.h` | ProjectState 新增 offlineFiles |
| 3 | `src/core/projectmanager.cpp` | stateToJson/jsonToState 序列化 resources.offline |
| 4 | `src/ui/offlineanalysistab.h/.cpp` | 新增 addFiles 公开方法 |
| 5 | `src/ui/transceivemodule.cpp` | invoke 新增 "addOfflineFiles" |
| 6 | `src/ui/mainwindow_project.cpp` | capture 多实例+离线文件+录制文件；apply 全量恢复 |
| 7 | `src/ui/mainwindow_setup.cpp` | recordingStopped 追加 recordFiles |
| 8 | `src/ui/panels/sidebarpanels.cpp` | ProjectPanel 离线文件节点 + extractOfflineFiles |
| 9 | `tests/test_canfileio.cpp` | 两个第三方样本用例 |
| 10 | `tests/test_project.cpp` | offlineFiles 往返断言 |
| 11 | `doc/测试报告.md` | 变更记录 |

## 八、风险与对策

| 风险 | 对策 |
| --- | --- |
| AscReader 重写破坏本软件格式闭环 | 现有 roundtrip 单测全量回归；写读格式样本（用户真实录制文件）人工验证 |
| CANoe 格式变体覆盖不全 | 以手头两个真实样本（7.0 / 15.7）为准，样本驱动；新变体后续按需补充 |
| 恢复顺序依赖（离线页需先创建） | openTabs 重建先于 addOfflineFiles；invoke 页面缺失静默忽略不崩溃 |
| 多实例恢复与 Flow 画布不同步 | 复用现有 addModuleInstance/rebuildScene 编排，仅去掉硬编码过滤 |
| 切换工程残留旧页面 | applyProjectState 增加收发四页关闭逻辑 |

## 九、工作量评估

- 问题 1：AscReader 重写 + 单测 ≈ 半天（解析矩阵已有现成参考实现，属移植）
- 问题 2：数据结构/序列化/捕获/恢复/树展示 + 测试 ≈ 1 天
- 合计约 1.5 个工作日
