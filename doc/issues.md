# openbus 问题跟踪账本（issues.md）

> 用途：记录用户反馈的功能 bug 与体验问题，持续跟进直至关闭。
> 状态标记：🔴 Open（待修复）/ 🟡 In Progress（修复中）/ 🟢 Fixed（已修复待验证）/ ⚪ Deferred（挂起/长期项）
> 每轮修复后更新状态与验证结论；历史已关闭问题移入文末归档区。

---

## 一、当前跟进问题

### T-01 🔴 设置下拉菜单过大遮挡表格（Trace）

- **现象**：FilterBar 设置按钮的下拉菜单单级平铺约 28 个菜单项（时间格式 5 项 + 说明文本、时间精度 5 项、刷新率 4 项、覆盖模式、错误帧高亮、着色规则、书签、清空列表），展开后过高遮挡报文表格。
- **根因**：`traceview.cpp` TraceTab 构造函数将所有设置项平铺在同一个 QMenu 中。
- **修复方案**：改为两级级联菜单（对齐 Wireshark View 菜单模式）——主菜单仅保留入口（时间格式 ▶ / 时间精度 ▶ / 刷新率 ▶ / 覆盖模式 / 错误帧高亮 / 着色规则 / 书签 / 清空列表），互斥选项移入子菜单；删除"说明"长文本项（改为子菜单 tooltip）。
- **涉及文件**：`src/ui/traceview.cpp`

### T-02 🔴 时间格式为"自上一显示分组"时按 Time 列排序结果错乱（Trace）

- **现象**：筛选某个 ID（如 0x399）后，时间格式切到"自上一显示分组"（微秒），再按 Time 列排序，排序结果与上下两行显示的间隔时间不符。
- **根因**（`cantraceproxymodel.cpp`）：
  1. 排序键与显示值**不一致**：`lessThan()` 的 SinceDisplay 分支用 `m_displayDeltas` 缓存（按**源序**"显示链"推导），而 `data()` 显示用 `displayDelta()`（按**当前代理序/排序后**实时推导），两者在排序后必然分叉；
  2. **循环依赖**：SinceDisplay 增量依赖显示顺序，显示顺序又依赖排序结果，以增量作排序键在数学上无稳定解；
  3. 新增帧路径下 `m_displayDeltas` 不更新（仅按 Time 列排序时更新），缓存 miss 时 fallback 到绝对时间戳，排序键随机混杂。
- **修复方案（对齐 Wireshark）**：Time 列排序键恒为帧的**绝对捕获时间戳**——显示格式（绝对/增量/日期/Unix）只改变显示文本，不改变排序语义（Wireshark Time 列即此行为）。删除 `m_displayDeltas`/`m_lastAcceptedSourceRow` 整套排序键缓存机制，`displayDelta()` 保留用于显示（O(1) 映射实时推导，排序/过滤后自动正确）。
- **涉及文件**：`src/models/cantraceproxymodel.cpp`、`src/models/cantraceproxymodel.h`

### T-03 🔴 Data 列宽无法拖拽调整（Trace）

- **现象**：拖动 Data 列右边缘无法改变列宽。
- **根因**：`traceview.cpp` `setModel()` 中将 Data 列设为 `QHeaderView::Stretch` 模式（Stretch 模式下用户拖拽无效）；`restoreColumnLayout()` 也因此跳过 Data 列宽度持久化。
- **修复方案**：Data 列改回 Interactive 模式（默认宽 400），宽度纳入列布局持久化。
- **涉及文件**：`src/ui/traceview.cpp`

### T-04 🔴 默认隐藏 Delta / Flags / Count / Signal 列（Trace）

- **现象**：首屏列过多，低频分析列（Delta/Flags/Count/Signal）挤占空间。
- **修复方案**：默认隐藏 `ColDelta`/`ColFlags`/`ColFrameCount`/`ColSignal`（列头右键可重新显示）；列布局版本升级 v3（旧布局丢弃，采用新默认列集——沿 v1→v2 处理惯例）。
- **涉及文件**：`src/ui/traceview.cpp`

### T-05 🟡 排序 / 筛选 / 分组全面对标 Wireshark & CANoe（Trace，持续项）

- **本轮已落实**：
  - Time 列排序键统一为绝对捕获时间戳（T-02，Wireshark 语义）；
  - "自上一显示分组"显示值随排序/过滤动态重算（`displayDelta()` 按当前显示序推导，Wireshark 语义）；
  - 稳定排序（`std::stable_sort`，等值保持捕获序）、3-state 排序（升/降/取消）、覆盖模式分组、Excel 风格值集过滤、列头漏斗过滤均已具备。
- **后续跟进候选**（按需排期）：
  - [ ] Delta 独立列排序语义复查（当前为捕获序差值，与列显示一致）
  - [ ] 筛选表达式与 Wireshark Display Filter 语法差异文档化
  - [ ] CANoe 风格"按 ID 分组折叠"显示模式
- **涉及文件**：`src/models/cantraceproxymodel.cpp`

### T-06 🔴 着色规则无效——"标记了颜色，实际没有任何颜色"（Trace）

- **现象**：配置着色规则（着色规则编辑器）后帧列表无任何颜色变化。
- **根因**（三个叠加缺陷）：
  1. **规则下标错位**（`cantracemodel.cpp` `evaluateColorRules`）：`m_colorFilters` 只收编"启用且编译成功"的规则，却用它的下标去索引全量 `m_colorRules`——禁用或编译失败的规则位于列表前部时，颜色张冠李戴；
  2. **编译失败静默丢弃**（`setColorRules`）：表达式写错（如大小写、全角符号、非法变量）时规则被静默跳过，无任何提示——用户以为规则生效，实际过滤器为空，正是"标记了颜色实际没颜色"的直接原因；
  3. **规则前景色完全无效**：`data()` 的 ForegroundRole 从不查询着色规则，规则里配置的前景色形同虚设。
- **修复方案**：
  1. `m_colorFilters` 与 `m_colorRules` 下标一一对齐（禁用/编译失败槽位置 nullptr），新增 `matchingColorRule()` 返回命中规则下标；
  2. 着色规则编辑器保存前逐条校验表达式（FilterEngine 编译），失败则弹出错误清单并阻止保存；编辑框增加实时校验状态行；
  3. ForegroundRole 优先返回命中规则的前景色。
- **涉及文件**：`src/models/cantracemodel.cpp`、`src/models/cantracemodel.h`、`src/ui/colorruleeditor.cpp`、`src/ui/colorruleeditor.h`

### T-07 🟢 设备连接默认隐式启动模拟器，造成数据源混淆

- **现象**：用户未主动打开 openbus 模拟器时，两处隐式启动造成混淆：
  1. 右侧面板快捷连接（onQuickConnect）：未配置真实设备时自动 `m_simulator->start()` 并显示"设备已连接 (模拟器)"；
  2. Flow 页开始测量（onMeasurementToggled）：硬件模式但设备未连接时自动启动模拟器作兑底——用户以为在采真实总线，实际看的是模拟数据。
- **修复方案**：移除两处隐式启动，改为输出面板引导提示（"请到设备连接页连接硬件，或显式连接 openbus 模拟器"）；保留三个显式入口：设备连接页点"连接"、工具栏"模拟器开关"、终端 `sim on`。测量可在无数据源时继续运行，设备连接后帧自动流入（onFrameReceived 仅门控测量状态）。
- **涉及文件**：`src/ui/mainwindow_actions.cpp`、`src/ui/mainwindow_frameflow.cpp`

### T-08 🟢 Flow 页功能块状态灯无数据流/异常表达（绿点仅常亮）

- **现象**：功能块（Filter/CAN parser/模块块）右侧绿点仅随使能常亮，无法表达"有数据流/正在处理"（需求：绿闪）与"运行异常"（需求：红闪）；未使能块还画灰点。
- **修复方案**（状态机，对齐用户需求 2026-08-24）：
  - **未使能**：不画灯（块体灰化已表达，灰点移除）；
  - **使能待命**：绿灯常亮（现状保留）；
  - **数据流活跃**（测量运行中且最近 1.5s 内有帧到达）：绿灯 500ms 相位闪烁；
  - **运行异常**：红灯闪烁（数据源块仅异常亮灯，正常不亮）；
  - 驱动：`MeasurementSetupView::onFrame`（帧到达即记录，接入既有 flowInvoke("onFrame") 分发链）+ 500ms 闪烁定时器（无流且无错时停转）；
  - 异常源（第一版）：设备错误（errorOccurred）→ source_real 块红闪（重连成功熄灭）；DBC 加载失败 → database 块红闪（重载成功熄灭）；新增 `setBlockError(blockId, on)` 通用接口经 flow 模块 setBlockError action 供壳侧后续扩展。
- **后续候选**：Trace 过滤表达式编译失败 → 对应 Trace 块红闪（跨模块链路待接）；设备运行中掉线检测（connectionChanged(false) 与主动断开区分）。
- **涉及文件**：`src/ui/measurementsetupview.h`、`src/ui/measurementsetupview.cpp`、`src/ui/flowmodule.cpp`、`src/ui/mainwindow_setup.cpp`

---

## 二、待验证遗留问题

### L-01 🟡 G14-P2：DBC 添加信号后 Graphic 无曲线（Graphic）

- **状态**：代码侧已加入 `[Graphic]` debug 日志链（onFrame 匹配/解码/入缓冲），等待用户复测并提供 `%APPDATA%\openbus\logs\openbus.log` 中的 `[Graphic]` 日志段定位。
- **备注**：2026-08-24 曾发现"修改未生效"实为主程序未重链（见 A-02），完整重链后用户尚未复测。

### L-02 ⚪ 模拟器模式下插件发送不可用（插件系统）

- **现象**：`CanDeviceManager::sendFrame()` 对 Simulator 直接 return false——模拟器模式下 UDS 插件无法发送帧（属功能缺失而非 bug）。
- **候选方案**：模拟器增加"注入帧"接口（sendFrame → 模拟器立即 frameGenerated 回环），可作为后续增强。

### L-03 ⚪ ZLG 枚举堆损坏崩溃（DEF-06）

- 详见 `doc/DEF-06复发-ZLG枚举堆损坏崩溃调查.md`；修复方案 A（进程隔离 probe）待评审。

---

## 三、已关闭问题归档

### A-01 🟢 G14 全系列（2026-08-24 关闭，代码在位）

| 子项 | 内容 | 结论 |
|---|---|---|
| P1 | Graphic 添加信号改为从已加载 DBC 数据库筛选（DbcSignalPickerDialog） | 已实现（见 `doc/G14-Issue-Status.md`） |
| P3a | hand/axis-fit-x/axis-fit-y/cursor-single/cursor-double 工具栏图标 | 已实现，SVG 注册于 resources.qrc |
| 卡尺 | 单/双卡尺图标去箭头；卡尺选定后贯穿所有纵轴分栏 | 已实现 |
| P3b | 拖动工具只动鼠标所在纵轴栏 → 全栏 X 轴同步平移（panOnlyX） | 已实现 |
| P2 | DBC 添加信号后无曲线 | 代码与日志在位，转入 L-01 待用户复测 |

### A-02 🟢 "G14 修改全部未生效"——主程序未重链（2026-08-24 关闭）

- **现象**：G14 全部修改在源码在位，用户运行却看不到任何变化。
- **根因**：上次构建只执行了 `build.py test`（仅构建 tests 聚合目标），`openbus.exe` 主程序从未重新链接；而 `resources.qrc`（全部图标）编译进主程序 → 用户运行的一直是旧版主程序。
- **修复**：执行 `build.py build` 完整构建（RCC 重编译 + 主程序重链）。
- **教训**：涉及 UI/资源变更必须 `build.py build`，不能只跑 `test`。

### A-03 🟢 启动报"找不到 libopenbus_graphic.dll"（2026-08-24 关闭）

- **根因**：两个构建进程并发操作同一 build 目录（后台 test 构建 + 手动 build 构建），链接器竞争导致 DLL 被删后未写回（data.dll 亦曾出现 0 字节损坏）。
- **修复**：清理全部构建/程序进程后单独重跑 `build.py build`。
- **教训**：禁止并发构建；构建前确认无残留 mingw32-make/cmake/ld/openbus 进程。

### A-04 🟢 UDS 插件发送的 CAN 报文在 Trace 不可见（2026-08-24 关闭）

- **现象**：UDS 插件点击"发送指令切换会话"，Trace 看不到发送的报文。
- **根因**（4 项叠加）：
  1. Tx 帧不回环——`onPluginSendFrame()` 只调 `sendFrame()`，不推显示链路；
  2. SDK `frames.send()` 不传 dlc → `jsonToFrame` 默认 0 → PEAK FD 驱动按 dlc 发空帧；
  3. 发送失败静默（设备未连接/未运行/模拟器均无提示）；
  4. `onFrameReceived()` 的 `m_measurementRunning` 门控（未点"开始测量"时所有帧不显示，属测量窗口语义，非 bug）。
- **修复**：`CanDeviceManager::sendFrame()` 发送成功后按 `m_startClock` 基准构造 Tx 回环帧（可选出参）；`onPluginSendFrame()` 发送成功 → `onFrameReceived(echo)`（Trace/Graphic/Flow 可见 Tx 帧），失败 → 输出面板细分提示；`pluginmanager.cpp` sendFrame 分支补 dlc（`lengthToDlc`）。
- **附带发现**：candle 驱动 recv 中 `echoId != kEchoIdRx → continue`（跳过硬件回环帧，注释"上层 send 路径已记账"）——上层此前从未记账，本次软件回环正补齐该设计意图，与 candle 无双重显示冲突。

---

## 四、变更记录

| 日期 | 摘要 |
|---|---|
| 2026-08-24 | 建账；录入 Trace 页面 6 项（T-01~T-06）并完成代码修复（T-05 部分落实）；归档 G14 系列（A-01）、构建产物两项（A-02/A-03）、插件 Tx 回环（A-04）；遗留 L-01~L-03 |
| 2026-08-24 | 录入并修复 T-07（模拟器隐式启动两处移除）、T-08（Flow 功能块状态灯状态机：待命常亮绿/数据流绿闪/异常红闪/未使能不亮） |
