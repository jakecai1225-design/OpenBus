# openbus — CAN/CAN FD 报文分析工具

<p align="center">
  <strong>专业的 CAN/CAN FD 总线报文录制、回放、解析与分析桌面软件</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Qt-6.8.3-green" alt="Qt 6.8.3">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.21+-orange" alt="CMake">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
</p>

---

## 简介

**openbus** 是一款灵感来源于 [Wireshark](https://www.wireshark.org/)、[CANoe](https://www.vector.com/canoe)、[Ozone](https://www.segger.com/products/development-tools/ozone-debugger/) 等优秀软件的 CAN/CAN FD 总线报文分析工具。采用 VS Code 风格的现代化 UI 设计，提供从报文录制到信号级解析的完整工作流，适用于汽车电子开发、总线调试、协议逆向等场景。
![alt text](image.png)

## 设计文档索引

| 文档 | 内容 | 对标 |
|------|------|------|
| [doc/Trace模块设计文档.md](doc/Trace模块设计文档.md) | 报文列表核心视图：架构分层、4 阶段性能优化（延迟格式化/批量刷新/增量过滤/环形缓冲）、书签着色、导航交互、CANoe 深度对标差距复查（§九） | CANoe Trace Window + Wireshark Packet List |
| [doc/Graphic模块设计文档.md](doc/Graphic模块设计文档.md) | 信号波形核心视图：多轴堆叠、卡尺测量、视口降采样、数学运算、游标联动 | CANoe Graphics Window |

## 功能特性

### 报文录制与回放
- **实时录制** — 连接 CAN 设备实时捕获总线报文，支持 CAN 2.0A/B 与 CAN FD
- **文件回放** — 加载 BLF/ASC/CSV/PCAP/TRC 报文文件，按原始时间戳精准回放，支持变速控制（0.1x ~ 10x）
- **进度拖拽** — 回放进度条可任意拖拽定位，快速跳转到关键时间点
- **文件导入** — 支持从 `.blf`（Vector 二进制日志）、`.asc`（Vector ASCII 日志）、`.csv` 文件导入报文到 Trace
- **文件导出** — 支持将 Trace 报文导出为 `.asc`、`.csv` 格式

### Trace 追踪
- **帧编号列 (No.)** — 参考 Wireshark，首列为帧序号，标识当前帧在捕获序列中的位置
- **绝对/相对时间** — 除绝对时间戳外，支持显示与上一帧的时间增量 (Delta)，方便排查周期不稳定等问题
- **Wireshark 风格列表** — No. / Time / Delta / Ch / Dir / ID / DLC / Data / Flags / Count 多列显示
- **按列排序** — 点击列标题升序/降序排序，支持数值与时间类型智能比较
- **表头漏斗过滤** — 鼠标悬停列标题时显示漏斗图标，点击即可快速设置该列筛选条件；已激活筛选的列持续显示高亮漏斗
- **按列筛选** — 每列独立筛选条件，ID 列支持 `>`、`<`、`!=` 运算符，快速过滤目标报文
- **快速筛选** — 右键列标题一键筛选仅 Rx/Tx、唯一 ID 列表
- **帧信息面板** — 选中报文显示完整解码字段，集成 DBC 信号级解析
- **十六进制转储** — 选中报文以 Offset + Hex + ASCII 格式展示原始数据
- **覆盖模式** — 同 CAN ID 只保留最新一帧，实时刷新数据值和帧计数
- **表达式过滤** — 类 Wireshark 过滤语法 `id == 0x123 && dlc > 4`，自写递归下降解析器
- **行标记与着色** — 过滤后可对结果行手动标记、自定义着色，方便分析对比；参考 Wireshark/CANoe 的标记着色机制
- **自动行着色** — 按 CAN ID / 方向 / 错误帧自动着色，视觉区分不同报文类型
- **搜索定位** — 按 ID、数据内容（Hex 模式）、时间范围搜索并跳转到目标帧
- **标记书签** — 手动标记重要帧，书签列表快速跳转
- **统计概览** — 总线负载率、各 ID 帧数/频率、错误帧统计
- **文件导入** — 支持 BLF / ASC / CSV 格式导入，自动解析时间戳与数据
- **文件导出** — 导出 Trace 为 ASC / CSV 格式，支持导出过滤后子集

### DBC 信号解析
- **DBC 加载** — 支持加载标准 `.dbc` 文件，自动解析报文与信号定义
- **信号树浏览** — 侧边栏 DBC 面板以树形结构展示 Message → Signal 层级
- **信号解码** — 选中 Trace 报文自动解码所有信号值（支持有符号/无符号、大小端）
- **信号双击追踪** — 双击 DBC 信号自动添加到 Graphic 图形视图实时绘制

### Graphic 图形视图
- **实时波形** — 以时间轴为横坐标绘制信号值变化曲线
- **多信号叠加** — 同时监控多个信号，支持通道独立配置
- **交互缩放** — 鼠标滚轮缩放、拖拽平移，支持波形测量

### 工程上下文管理
- **多工程并行** — 创建多个分析工程，每个工程独立管理 CAN 配置、DBC 文件、布局
- **快速切换** — 一键切换工程上下文，无需重新加载文件
- **工程持久化** — 工程配置保存为 `.openbusproj` 文件，下次打开即恢复工作状态

### 工具集
- **侧边栏工具集入口** — ActivityBar 中的工具集图标，点击展开工具列表面板
- **BLF ↔ ASC ↔ CSV 转换** — 报文日志文件格式互转工具，后台线程执行，不阻塞 UI
- **DBC 查看编辑** — 独立打开任意 DBC 文件，树形浏览报文/信号层级，可编辑信号属性并保存
- **报文统计分析** — 加载日志文件统计各 CAN ID 帧数、频率、周期与抖动
- **ID 频率/周期分析** — 按 CAN ID 统计报文周期均值/最大/最小/标准差
- **总线负载率** — 基于波特率与数据量计算总线负载率
- **DBC 信号清单导出** — 从 DBC 导出全部报文/信号清单为 CSV/Markdown
- **独立运行** — 工具独立运行，不影响当前工程数据，后续持续集成更多总线分析工具

### VS Code 风格 UI
- **无边框窗口** — 去除原生标题栏，菜单栏直接置顶，最小化/最大化/关闭按钮位于菜单栏右上角
- **全局侧边栏** — ActivityBar + 可折叠 SideBar，按功能分组面板
- **可拆分编辑器** — 主区域标签页支持右键"向右拆分"/"向下拆分"，并排对比视图
- **全面板可停靠** — 左侧、右侧、底部、帧信息、HexDump 面板均可关闭、拖拽、停靠
- **Aero Snap** — 无边框窗口仍支持 Windows 原生窗口贴靠、边缘缩放

### 其他功能
- **命令行终端** — 内置命令行界面，支持 `help`、`clear`、`sim on/off`、`record`、`play`、`filter` 等命令
- **CAN 模拟器** — 内置报文模拟器，可生成测试报文用于功能验证
- **统计面板** — 实时显示总帧数、Rx/Tx 分布、CAN FD / Extended 统计
- **主题样式** — QSS 驱动的暗色菜单栏 + 亮色内容区，可自定义主题

---

## 对标分析：CANoe & TSMaster 核心 20% 功能

> 帕累托法则：20% 的功能覆盖 80% 的日常使用场景。以下梳理 CANoe（Vector）和 TSMaster（TOSUN）最核心的功能，逐项对标 openbus 当前状态与实施方案。

### 一、竞品核心功能矩阵

#### CANoe (Vector) — 行业标杆

CANoe 是 Vector 旗舰级 CAN 总线开发工具，覆盖测量、分析、仿真、测试全流程。其核心 20% 功能如下：

| # | 功能模块 | CANoe 能力 | openbus 状态 | 差距分析 |
|---|---------|-----------|---------|--------|
| C1 | **Trace Window** | 实时报文列表：Delta 时间、行着色、表达式过滤、预定义过滤器、快速搜索、书签标记 | ✅ 基础已实现 | 缺少预定义过滤器集、书签持久化、动态着色规则编辑器 |
| C2 | **Graphics Window** | 信号曲线：多 Y 轴、双游标测量、视口降采样、信号数学运算、导出图片 | ✅ 基础已实现 | 缺少游标测量、多 Y 轴、视口降采样、信号数学运算 |
| C3 | **Data Window** | 信号/系统变量实时表格：当前值、最小/最大值、原始值与物理值并列显示 | ⬜ 未实现 | 新增功能，需设计信号表格数据模型 |
| C4 | **Measurement Setup** | 通道树配置：波特率、采样点、CAN FD、触发条件、硬件通道映射 | ✅ 已实现 | 缺少触发条件配置（基于 ID/数据/错误的触发录制） |
| C5 | **Online/Offline** | 在线采集 ↔ 离线回放共享同一套上层分析逻辑，无缝切换 | ✅ 已实现 | 架构层面已具备，DataCore 统一数据流设计已就绪 |
| C6 | **Interactive Send (IGS)** | 交互式信号发送：面板控件（按钮、滑块、输入框）→ 信号值 → 周期发送报文 | ✅ 基础已实现 | 缺少面板设计器（可视化拖拽创建交互面板） |
| C7 | **CAPL Scripting** | C 语法脚本引擎：事件驱动（on message/on key/on timer）、报文收发、信号读写、文件 I/O | ⬜ 未实现 | 核心差距，需引入脚本引擎（Lua/sol2 方案） |
| C8 | **Bus Statistics** | 总线负载率、各 ID 帧数/频率、周期/抖动、错误帧分类统计 | ✅ 基础已实现 | 缺少周期抖动分析（均值/最大/最小/标准差）和错误帧分类统计 |
| C9 | **Log File I/O** | BLF 原生录制、ASC 导入导出、日志文件分片、条件触发录制 | ✅ 已实现 | 缺少文件分片（按大小/时间自动切割）和条件触发录制 |
| C10 | **Filter & Analysis** | 表达式过滤（类似 Wireshark）、预定义过滤集、分析窗口（差值计算） | ✅ 已实现 | 缺少预定义过滤集管理和差值分析窗口 |

#### TSMaster (TOSUN) — 新兴竞品

TSMaster 是 TOSUN 推出的开放总线工具平台，支持多厂商硬件，免费用于科研教育。其核心 20% 功能如下：

| # | 功能模块 | TSMaster 能力 | openbus 状态 | 差距分析 |
|---|---------|-------------|---------|--------|
| T1 | **Trace Window** | 报文列表：覆盖模式、行着色、列筛选、实时刷新 | ✅ 已实现 | 功能基本对齐 |
| T2 | **Graphics** | 信号曲线：多通道叠加、缩放平移 | ✅ 基础已实现 | 缺少多 Y 轴和游标联动 |
| T3 | **Signal Browser** | DBC 信号树：报文→信号层级浏览 | ✅ 已实现 | 功能基本对齐 |
| T4 | **Measurement Setup** | 通道映射、波特率、CAN FD 配置 | ✅ 已实现 | 功能基本对齐 |
| T5 | **Online Replay** | 在线采集 + 离线回放 | ✅ 已实现 | 功能基本对齐 |
| T6 | **Interactive Send** | 信号/报文发送面板 | ✅ 基础已实现 | 缺少面板设计器 |
| T7 | **C/C++ Script Editor** | 内嵌 C/C++ 脚本编辑器，编译执行自动化测试 | ⬜ 未实现 | 核心差距，与 CANoe CAPL 类似的自动化能力 |
| T8 | **Statistics Window** | 总线负载、帧率、错误帧统计 | ✅ 基础已实现 | 缺少周期抖动和错误帧分类 |
| T9 | **Bus Logging** | BLF/ASC 日志录制 | ✅ 已实现 | 功能基本对齐 |
| T10 | **File Converter** | DBC/ARXML/XLS/XLSX/DBF/YAML 格式互转 | 🔄 部分实现 | 仅支持 DBC，缺少 ARXML/XLS 格式转换 |
| T11 | **Panel Designer** | 可视化拖拽创建交互面板（按钮/滑块/图表/输入框） | ⬜ 未实现 | 与 CANoe IGS 面板设计器类同 |
| T12 | **Test System** | 自动化测试序列：步骤化测试用例、通过/失败判定、报告生成 | ⬜ 未实现 | 核心差距，对标 CANoe Test Feature Set |

### 二、差距优先级矩阵

按 **用户价值 × 实现可行性** 排序，识别最值得投入的功能：

| 优先级 | 功能 | 来源 | 用户价值 | 实现难度 | 预计工时 |
|-------|------|------|---------|---------|---------|
| 🔴 P0 | **Data Window（信号实时表格）** | CANoe C3 | ⭐⭐⭐⭐⭐ | 🟢 低 | 2-3 天 |
| 🔴 P0 | **Graphics 游标测量 + 多 Y 轴** | CANoe C2 / TSMaster T2 | ⭐⭐⭐⭐⭐ | 🟡 中 | 3-5 天 |
| 🔴 P0 | **Bus Statistics 增强（周期抖动 + 错误帧分类）** | CANoe C8 | ⭐⭐⭐⭐ | 🟢 低 | 2-3 天 |
| 🟡 P1 | **Log File 分片 + 条件触发录制** | CANoe C9 | ⭐⭐⭐⭐ | 🟡 中 | 3-4 天 |
| 🟡 P1 | **预定义过滤集管理** | CANoe C10 | ⭐⭐⭐⭐ | 🟢 低 | 1-2 天 |
| 🟡 P1 | **Trace 书签持久化 + 动态着色规则编辑器** | CANoe C1 | ⭐⭐⭐ | 🟢 低 | 2-3 天 |
| 🟡 P1 | **File Converter（ARXML 支持）** | TSMaster T10 | ⭐⭐⭐ | 🟡 中 | 5-7 天 |
| 🟠 P2 | **Panel Designer（交互面板设计器）** | CANoe C6 / TSMaster T11 | ⭐⭐⭐⭐⭐ | 🔴 高 | 7-14 天 |
| 🟠 P2 | **CAPL/脚本引擎（Lua + sol2）** | CANoe C7 / TSMaster T7 | ⭐⭐⭐⭐⭐ | 🔴 高 | 10-15 天 |
| 🟠 P2 | **Test System（自动化测试序列）** | TSMaster T12 | ⭐⭐⭐⭐ | 🔴 高 | 10-15 天 |

### 三、核心功能实施方案

#### 方案 1：Data Window — 信号实时表格（P0，对标 CANoe Data Window）

**目标**：以表格形式实时展示多个信号的当前值、最小值、最大值、原始值与物理值。

**UI 布局**：
```
┌──────────────────────────────────────────────────────────────────┐
│ Data Window                                                       │
├──────────┬─────────┬──────────┬──────────┬──────────┬───────────┤
│ Signal   │ Current │ Raw      │ Physical │ Min      │ Max       │
├──────────┼─────────┼──────────┼──────────┼──────────┼───────────┤
│ EngineRPM│ 3250    │ 0x0CAA   │ 3250 rpm │ 0        │ 6500      │
│ Throttle │ 45.2    │ 0xB5     │ 45.2 %   │ 0.0      │ 100.0     │
│ BrakeSt  │ 1       │ 0x01     │ ON       │ 0        │ 1         │
└──────────┴─────────┴──────────┴──────────┴──────────┴───────────┘
```

**实现方案**：
- 新增 `DataWindow` 类，继承 `QWidget`，内嵌 `QTableWidget`
- 数据源：订阅 `DbcManager` 信号解码结果 + `CanTraceModel` 覆盖模式最新帧
- 信号列表来源：用户从 DBC 信号树拖拽或双击添加（复用现有 Graphic 信号添加交互）
- 刷新策略：定时器 100ms 轮询最新帧数据（而非每帧触发），避免高频报文导致 UI 卡顿
- 最小/最大值：在 `DataWindow` 内部维护每个信号的统计缓存

**涉及文件**：
- 新增：`src/ui/datawindow.h` / `src/ui/datawindow.cpp`
- 修改：`src/ui/mainwindow.cpp`（注册标签页类型）
- 修改：`src/ui/spliteditorarea.cpp`（支持 Data 标签页创建）

---

#### 方案 2：Graphics 游标测量 + 多 Y 轴（P0，对标 CANoe Graphics Window）

> **详细设计已分离至 [doc/Graphic模块设计文档.md](doc/Graphic模块设计文档.md)**
>
> 当前进度：单/双卡尺测量（ΔT/ΔY/f）、每信号独立 Y 轴堆叠、信号列表联动、卡尺插值取值均已实现 ✅；
> 待实现：视口降采样（P0）、多视图游标联动（P1）、信号数学运算（P1）。实施计划与性能目标见设计文档。

---

#### 方案 3：Bus Statistics 增强（P0，对标 CANoe Bus Statistics Window）

**目标**：在现有统计面板基础上增加周期抖动分析和错误帧分类统计。

**统计表格**：
```
┌──────────┬──────┬────────┬──────────┬──────────┬──────────┬──────────┐
│ CAN ID   │ 帧数 │ 平均周期│ 最小周期 │ 最大周期 │ 抖动(σ) │ 频率(Hz) │
├──────────┼──────┼────────┼──────────┼──────────┼──────────┼──────────┤
│ 0x100    │ 1240 │ 10.0ms │ 9.8ms    │ 10.5ms   │ 0.12ms   │ 100.0    │
│ 0x1A5    │  620 │ 16.1ms │ 15.2ms   │ 17.8ms   │ 0.45ms   │ 62.1     │
│ 0x2FF    │   15 │ —      │ —        │ —        │ —        │ 0.5 (事件)│
└──────────┴──────┴────────┴──────────┴──────────┴──────────┴──────────┘

错误帧统计：
┌──────────────┬──────┬──────────────┐
│ 错误类型     │ 数量 │ 占比          │
├──────────────┼──────┼──────────────┤
│ Stuff Error  │    3 │ 30.0%        │
│ Form Error   │    2 │ 20.0%        │
│ ACK Error    │    5 │ 50.0%        │
│ Bit0 Error  │    0 │ 0.0%         │
│ Bit1 Error  │    0 │ 0.0%         │
│ CRC Error    │    0 │ 0.0%         │
└──────────────┴──────┴──────────────┘
```

**实现方案**：
- 新增 `BusStatistics` 类，在后台线程统计
- 周期计算：每个 CAN ID 维护一个 `QQueue<uint64_t>` 时间戳队列，计算相邻帧时间差
- 抖动 (σ)：周期的标准差 `sqrt(Σ(xi-x̄)²/N)`
- 错误帧分类：解析 `CanFrame::flags` 中的错误标志位（ECC 错误码）
- 总线负载率：`(总数据位数 / (统计时长 × 波特率)) × 100%`
  - 总数据位数 = Σ(47 + DLC×8) for each frame（经典 CAN 帧位数）
- 刷新策略：1s 定时器触发统计计算，避免每帧更新

**涉及文件**：
- 新增：`src/core/busstatistics.h` / `src/core/busstatistics.cpp`
- 修改：`src/ui/tools/loganalysisview.h` / `src/ui/tools/loganalysisview.cpp`（替换现有 FrameStatisticsView 桩实现）
- 修改：`src/ui/mainwindow.cpp`（对接统计服务信号槽）

---

#### 方案 4：Log File 分片 + 条件触发录制（P1，对标 CANoe Logging）

**目标**：录制时支持按文件大小/时间自动分片，支持基于表达式条件的触发录制。

**分片策略**：
- 按大小分片：文件达到配置阈值（默认 50 MB）时自动切割，文件名追加序号
- 按时间分片：录制达到配置时长（默认 10 分钟）时自动切割
- 环形模式：达到最大文件数时覆盖最旧文件（对标 CANoe Ring Buffer）

**条件触发录制**：
- 前置缓冲 (Pre-Trigger)：始终在内存中缓存最近 N 秒/N 帧的报文
- 触发条件：复用现有 `FilterEngine` 表达式引擎
  - 示例：`id == 0x1A5 && data[0] == 0xFF`（特定报文特定数据触发）
  - 示例：`error == true`（错误帧出现时触发）
- 触发后行为：将前置缓冲写入文件 → 持续录制 → 达到后置时长后停止

**实现方案**：
- 新增 `LogSplitter` 类，管理文件分片逻辑
- 新增 `TriggerRecorder` 类，封装前置缓冲 + 触发条件 + 后置录制
- 复用 `FilterEngine` 解析触发条件表达式
- UI：在 RecordTab 中增加「分片设置」和「触发录制」折叠面板

**涉及文件**：
- 新增：`src/core/logsplitter.h` / `src/core/logsplitter.cpp`
- 新增：`src/core/triggerrecorder.h` / `src/core/triggerrecorder.cpp`
- 修改：`src/ui/recordtab.h` / `src/ui/recordtab.cpp`

---

#### 方案 5：预定义过滤集管理（P1，对标 CANoe Filter Presets）

> **已分离至设计文档：[doc/Trace模块设计文档.md](doc/Trace模块设计文档.md) §4.3**
>
> `.sfilter` 预设文件格式、FilterPresetManager 设计详见 Trace 设计文档。

---

#### 方案 6：Trace 书签持久化 + 着色规则编辑器（P1，对标 CANoe Trace 标记着色）

> **详细设计已分离至 [doc/Trace模块设计文档.md](doc/Trace模块设计文档.md) §4.1 / §4.2**
>
> 现状：运行时标记/行标签/着色规则引擎（`ColorRule{expr, background, foreground, enabled}`，首个命中生效）已实现 ✅；
> 待实现：书签工程文件序列化（`{seqCounter, note, timestamp, color}`）、着色规则编辑器 UI、规则持久化到 `.openbusproj`。
> 编辑器线框图与涉及文件清单见设计文档。

---

#### 方案 7：File Converter — ARXML 格式支持（P1，对标 TSMaster File Converter）

**目标**：支持 ARXML（AUTOSAR XML）格式的 DBC 导入导出，覆盖现代 ECU 开发流程。

**ARXML 格式说明**：
- AUTOSAR 标准文件格式，基于 XML
- 描述 ECU 通信（PDU、Signal、ISignal、IPdu、Frame 等）
- 比 DBC 更复杂，支持 CAN FD、以太网等
- 使用 `pugixml`（MIT 协议）解析 XML

**实现方案**：
- 引入 `pugixml`（MIT，单头文件 + 单源文件）到 `third_party/pugixml/`
- 新增 `ArxmlImporter` 类，解析 ARXML → 转换为内部 `DbcData` 结构
- 新增 `ArxmlExporter` 类，反向导出
- 支持批量转换：菜单「工具 → 文件格式转换」中增加 ARXML 选项

**涉及文件**：
- 新增：`third_party/pugixml/`（pugixml.hpp + pugixml.cpp + pugiconfig.hpp）
- 新增：`src/core/dbc/arxml_importer.h` / `src/core/dbc/arxml_importer.cpp`
- 新增：`src/core/dbc/arxml_exporter.h` / `src/core/dbc/arxml_exporter.cpp`
- 修改：`src/ui/tools/blfasconverter.h` / `src/ui/tools/blfasconverter.cpp`（扩展为通用文件转换器）
- 修改：`third_party/Dependencies.cmake`（添加 pugixml 目标）

---

#### 方案 8：Panel Designer — 交互面板设计器（P2，对标 CANoe Panels / TSMaster Panel）

**目标**：可视化拖拽创建交互面板，通过控件（按钮、滑块、输入框、仪表盘）发送信号/报文。

**面板设计器**：
- 左侧控件库：按钮、滑块、输入框、下拉框、开关、仪表盘、LED 指示灯
- 中间画布：拖拽控件到画布，自由布局
- 右侧属性：绑定信号/报文、范围、步进、样式
- 面板保存为 `.spanel` 文件（JSON 格式）

**运行时**：
- 面板以标签页方式打开，控件值变化 → 信号值更新 → 周期发送或手动发送
- 支持面板控件的信号值回显（从 Trace 数据更新控件状态）

**面板文件格式**（`.spanel`）：
```jsonc
{
  "version": 1,
  "name": "制动控制面板",
  "controls": [
    {
      "type": "slider",
      "name": "BrakePressure",
      "x": 20, "y": 20, "w": 300, "h": 40,
      "signal": {"dbc": "brake.dbc", "message": "BrakeCmd", "signal": "Pressure"},
      "range": {"min": 0, "max": 100, "step": 1},
      "sendMode": "onRelease"
    },
    {
      "type": "button",
      "name": "EmergencyBrake",
      "x": 20, "y": 80, "w": 120, "h": 40,
      "signal": {"dbc": "brake.dbc", "message": "BrakeCmd", "signal": "Emergency"},
      "sendMode": "onPress",
      "value": 1
    },
    {
      "type": "led",
      "name": "BrakeStatus",
      "x": 160, "y": 80, "w": 30, "h": 30,
      "signal": {"dbc": "brake.dbc", "message": "BrakeSts", "signal": "Active"},
      "onColor": "#00FF00", "offColor": "#333333"
    }
  ]
}
```

**实现方案**：
- 新增 `PanelDesigner` 类：设计模式画布，拖拽布局控件
- 新增 `PanelRuntime` 类：运行模式，加载 `.spanel` 并实例化控件
- 控件库：每种控件一个类（`PanelButton`、`PanelSlider`、`PanelLed`、`PanelGauge`）
- 信号绑定：控件值变化 → 调用 `DbcManager::encodeSignal()` → 构造 `CanFrame` → `CanDeviceManager::send()`
- 面板可作为独立标签页或 Dock 面板

**涉及文件**：
- 新增：`src/ui/panels/paneldesigner.h` / `src/ui/panels/paneldesigner.cpp`
- 新增：`src/ui/panels/panelruntime.h` / `src/ui/panels/panelruntime.cpp`
- 新增：`src/ui/panels/panelcontrols.h` / `src/ui/panels/panelcontrols.cpp`
- 新增：`src/core/panelmanager.h` / `src/core/panelmanager.cpp`

---

#### 方案 9：脚本引擎 — Lua + sol2（P2，对标 CANoe CAPL / TSMaster C++ Script）

**目标**：嵌入 Lua 脚本引擎，支持事件驱动（报文接收、按键、定时器）的自动化测试。

**CAPL 能力对标**：

| CAPL 能力 | openbus Lua 等价实现 | 说明 |
|---------|-----------------|------|
| `on message CAN1.0x123` | `openbus.on_message(0x123, function(msg) ... end)` | 报文事件回调 |
| `on key 'a'` | `openbus.on_key('a', function() ... end)` | 按键事件回调 |
| `on timer T1` | `openbus.on_timer(1000, function() ... end)` | 定时器事件 |
| `output(0x456, ...)` | `openbus.send(0x456, {0x01, 0x02})` | 发送报文 |
| `$Signal::RPM` | `openbus.signal("RPM").value` | 读写信号值 |
| `write("...")` | `openbus.log("...")` | 日志输出 |
| `if (this.id == 0x123)` | `if msg.id == 0x123 then` | 条件判断 |

**Lua 脚本示例**：
```lua
-- 自动制动测试用例
openbus.on_message(0x100, function(msg)
  local rpm = openbus.signal("EngineRPM").value
  if rpm > 5000 then
    openbus.log("WARN: RPM 过高: " .. rpm)
    openbus.send(0x1A5, {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})
  end
end)

openbus.on_timer(1000, function()
  openbus.log("当前总线负载: " .. openbus.bus_load() .. "%")
end)

openbus.on_key('F5', function()
  openbus.log("开始测试序列...")
  openbus.send(0x200, {0x01})
  openbus.wait(100)
  openbus.send(0x200, {0x00})
end)
```

**实现方案**：
- 引入 `sol2`（MIT）到 `third_party/sol2/`
- 引入 `lua`（MIT）到 `third_party/lua/`（预编译 Windows DLL + 源码）
- 新增 `ScriptEngine` 类：封装 sol2 state，注册 openbus API 绑定
- 新增 `ScriptEditor` 类：内嵌代码编辑器（语法高亮可选，先做纯文本 + 行号）
- 脚本以标签页方式打开，F5 执行，底部输出窗口显示日志
- 事件绑定：`ScriptEngine` 订阅 `DataCore` 报文流，匹配 Lua 回调

**涉及文件**：
- 新增：`third_party/sol2/`（sol2 头文件）
- 新增：`third_party/lua/`（lua 5.4 源码 + CMake 集成）
- 新增：`src/core/scriptengine.h` / `src/core/scriptengine.cpp`
- 新增：`src/ui/scripteditor.h` / `src/ui/scripteditor.cpp`
- 修改：`third_party/Dependencies.cmake`（添加 lua + sol2 目标）
- 修改：`src/ui/bottompanel.cpp`（增加脚本输出标签页）

---

#### 方案 10：Test System — 自动化测试序列（P2，对标 TSMaster Test System / CANoe Test Feature Set）

**目标**：步骤化测试用例编排，自动执行、结果判定、报告生成。

**测试用例格式**（`.stest` JSON）：
```jsonc
{
  "version": 1,
  "name": "制动系统回归测试",
  "setup": [
    {"action": "load_dbc",    "file": "brake.dbc"},
    {"action": "load_log",    "file": "baseline.blf"},
    {"action": "start_replay"}
  ],
  "steps": [
    {
      "name": "验证制动压力响应",
      "wait_signal": {"signal": "BrakePressure", "timeout": 5000},
      "assert": {"signal": "BrakePressure", "op": ">", "value": 50},
      "on_pass": {"log": "制动压力正常"},
      "on_fail": {"log": "制动压力异常", "severity": "error"}
    },
    {
      "name": "验证紧急制动触发",
      "send": {"id": "0x200", "data": [1]},
      "wait_signal": {"signal": "EmergencyBrake", "timeout": 1000},
      "assert": {"signal": "EmergencyBrake", "op": "==", "value": 1}
    }
  ],
  "teardown": [
    {"action": "stop_replay"}
  ]
}
```

**实现方案**：
- 新增 `TestEngine` 类：解析 `.stest` 文件，顺序执行步骤
- 动作类型：`load_dbc`、`load_log`、`start_replay`、`stop_replay`、`send`、`wait_signal`、`assert`、`wait`
- 测试报告：生成 HTML/CSV 格式报告（通过/失败/耗时/日志）
- UI：左侧测试用例列表，右侧步骤详情，底部执行日志
- 测试可嵌套调用 Lua 脚本（与方案 9 脚本引擎联动）

**涉及文件**：
- 新增：`src/core/testengine.h` / `src/core/testengine.cpp`
- 新增：`src/ui/testview.h` / `src/ui/testview.cpp`
- 新增：`src/core/testreporter.h` / `src/core/testreporter.cpp`

---

### 四、实施路线图

```
Phase 1 (近期 1-2 周) — P0 核心体验补齐
├── Data Window 信号实时表格        ← 方案 1
├── Graphics 游标测量 + 多 Y 轴     ← 方案 2
└── Bus Statistics 周期抖动增强     ← 方案 3

Phase 2 (中期 2-4 周) — P1 功能完善
├── Log File 分片 + 触发录制       ← 方案 4
├── 预定义过滤集管理              ← 方案 5
├── Trace 书签 + 着色规则编辑器    ← 方案 6
└── ARXML 格式支持                ← 方案 7

Phase 3 (长期 1-2 月) — P2 高级能力
├── Panel Designer 交互面板设计器  ← 方案 8
├── Lua 脚本引擎 (sol2)           ← 方案 9
└── Test System 自动化测试序列     ← 方案 10
```

### 五、对标总结

| 维度 | CANoe | TSMaster | openbus (当前) | openbus (Phase 3 后) |
|------|-------|---------|-----------|-----------------|
| 报文追踪 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 信号图形 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 信号表格 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐ | ⭐⭐⭐⭐ |
| DBC 解析 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 录制回放 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 交互发送 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 自动化脚本 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐ | ⭐⭐⭐⭐ |
| 总线统计 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 文件格式 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 测试系统 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐ | ⭐⭐⭐⭐ |
| UI 灵活性 | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| 开源/免费 | ❌ | 🔄 部分 | ✅ | ✅ |

**openbus 差异化优势**：
1. **VS Code 式自由布局** — CANoe/TSMaster 布局固定，openbus 全面板可拖拽停靠
2. **Wireshark 风格过滤引擎** — 自研递归下降解析器，语法更直观
3. **开源 MIT 协议** — CANoe 年费数万，openbus 完全免费
4. **跨平台潜力** — Qt 天然支持 Linux/macOS，CANoe 仅 Windows
5. **现代化 UI** — VS Code 风格暗色主题，视觉体验优于 CANoe 传统界面

---

## Trace 页面详细规划

> **完整设计已分离至 [doc/Trace模块设计文档.md](doc/Trace模块设计文档.md)** — 对标分析、架构分层、性能优化方案、待实现功能规划、实施计划。
>
> 当前状态概要：
>
> | 领域 | 状态 |
> |------|------|
> | Wireshark 风格列表/排序/列筛/表达式过滤/快速筛选 | ✅ 已实现 |
> | 文件导入导出（BLF/ASC/CSV，含过滤子集导出） | ✅ 已实现 |
> | 行标记/着色/标签（seqCounter 键）+ 着色规则引擎 | ✅ 已实现（编辑器 UI 待实现） |
> | 覆盖模式 / 时间参考点 / 5 种时间戳模式 | ✅ 已实现 |
> | 视窗缩略图导航（ViewportOverview）+ 视窗虚拟表格 | ✅ 已实现 |
> | 性能优化 Phase 1 延迟格式化 / Phase 2 批量刷新 / Phase 4 环形缓冲 | ✅ 已实现 |
> | Phase 3 增量过滤代理 `CanTraceProxyModel`（替代 QSortFilterProxyModel，新增行 O(1) 过滤） | ✅ 已实现（2026-08-17） |
> | 值勾选列筛选 / 时间精度配置 / 书签文件导出导入 / 标记跳转菜单 | ✅ 已实现（2026-08 复查确认） |
> | CANoe 深度复查速赢：Name 列 / 字体缩放 / 标记键盘导航（Ctrl+./Ctrl+,）/ 默认语义配色（error/Tx）/ `error` 过滤标识符 | ✅ 已实现（2026-08-17） |
> | Trace Explorer 底部标签外壳（详情/信号/统计/差异，T8） | ✅ 已实现（2026-08-17） |
> | 选中帧统计视图（时间 Δt + 信号/字节 min/max/avg/σ/首末，T9）/ 选中帧差异对比（字节级 + 信号级首末对比，T10） | ✅ 已实现（2026-08-17） |
> | 书签工程集成 / 预定义过滤集 / QHexEdit2 | ⬜ 待实现 |
> | CANoe 深度复查其余项：Pass/Stop 过滤组、结构化详情树、图标工具栏、混合事件流等 | ⬜ 差距清单与实施建议见 [设计文档 §九](doc/Trace模块设计文档.md) |

---

## 软件架构

```
openbus/
├── CMakeLists.txt              # 顶层 CMake 构建配置
├── src/
│   ├── main.cpp                # 程序入口
│   ├── core/                   # 核心层 — 数据结构与引擎
│   │   ├── canframe.h          #   CAN/CAN FD 帧结构
│   │   ├── recorder            #   报文录制器
│   │   ├── player              #   报文回放器
│   │   ├── cansimulator        #   CAN 模拟器
│   │   ├── dbcdata.h           #   DBC 数据结构
│   │   ├── dbcmanager          #   DBC 文件管理器
│   │   └── canfileio/          #   文件格式 I/O 层
│   │       ├── canfileio       #   读写器接口 + 格式枚举
│   │       ├── blf             #   BLF 格式读写 (Vector 二进制)
│   │       ├── asc             #   ASC 格式读写 (Vector ASCII)
│   │       ├── csv             #   CSV 格式读写 (通用文本)
│   │       ├── pcap_reader     #   PCAP 格式读取 (libpcap 网络捕获)
│   │       └── trc_reader      #   TRC 格式读取 (Vector 旧格式)
│   ├── models/                 # 数据模型层
│   │   ├── cantracemodel       #   Trace 表格模型
│   │   └── canfilterproxymodel #   过滤代理模型（排序+筛选）
│   ├── ui/                     # 界面层
│   │   ├── mainwindow          #   主窗口（无边框 + Dock 布局）
│   │   ├── spliteditorarea     #   可拆分编辑器区域
│   │   ├── traceview           #   Trace 列表 + 帧信息 + HexDump
│   │   ├── graphicview         #   信号图形视图
│   │   ├── filterbar           #   过滤栏
│   │   ├── activitybar         #   左侧活动栏
│   │   ├── bottompanel         #   底部面板（终端/输出/问题）
│   │   ├── rightpanel          #   右侧属性面板
│   │   └── panels/
│   │       └── sidebarpanels   #   侧边栏面板（工程/DBC/配置/设备）
│   └── utils/
│       └── canutils            #   格式化与工具函数
├── resources/
│   ├── resources.qrc           # Qt 资源集合
│   └── styles/
│       └── default.qss         # 全局样式表（VS Code 风格）
└── scripts/
    └── build.py                # Python 构建脚本
```

## 安装教程

### 依赖环境

- [Qt 6.8+](https://www.qt.io/download-open-source)（安装时勾选 MinGW 组件）
- [CMake 3.21+](https://cmake.org/download/)
- [MinGW 13+](https://www.mingw-w64.org/) 或 MSVC 2022

### 构建步骤

```bash
# 1. 配置（指定 Qt6 路径和编译器）
cmake -B build -S . -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64" \
  -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_C_COMPILER="D:/Qt/Tools/mingw1310_64/bin/gcc.exe"

# 2. 编译
cmake --build build

# 3. 部署（Windows）
D:/Qt/6.8.3/mingw_64/bin/windeployqt.exe build/bin/openbus.exe

# 4. 运行
./build/bin/openbus.exe
```

> 也可使用项目内置的 Python 构建脚本：`python scripts/build.py all`

## 使用说明

1. **启动程序** — 打开 openbus，界面分为左侧边栏、中央编辑区、右侧属性面板、底部输出面板
2. **加载 DBC** — 通过「文件 → 打开文件」加载 `.dbc` 信号定义文件
3. **加载报文文件** — 打开 BLF/ASC/CSV/PCAP/TRC 等格式的报文文件，报文自动填充到 Trace 列表
4. **回放分析** — 在左侧「回放控制」折叠栏点击播放，使用速度下拉框调节回放速率
5. **筛选报文** — 在过滤栏输入表达式（如 `id == 0x123`），或右键列标题按列筛选
6. **查看信号** — 选中 Trace 报文，底部帧信息面板自动显示解码后的信号值
7. **图形监控** — 双击 DBC 信号树中的信号项，自动添加到 Graphic 视图绘制波形
8. **工程管理** — 在左侧「工程」面板创建多个分析工程，快速切换工作上下文

## 快捷操作

| 操作 | 方式 |
|------|------|
| 拖拽窗口 | 按住菜单栏空白区域拖动 |
| 最大化/还原 | 双击菜单栏空白区域 |
| 拆分标签页 | 右键标签栏 → 「向右拆分」/「向下拆分」 |
| 按列排序 | 点击列标题 |
| 按列筛选 | 右键列标题 → 「筛选...」 |
| 快速过滤 ID | 右键 ID 列标题 → 选择唯一 ID |
| 清空 Trace | 工具菜单 → 「清空 Trace」或命令行输入 `clear` |

## 技术栈

| 组件 | 版本/说明 |
|------|-----------|
| Qt | 6.8.3 (Widgets) |
| C++ | 17 |
| CMake | 3.21+ |
| 编译器 | MinGW 13.1.0 / MSVC 2022 |
| 构建系统 | CMake + MinGW Makefiles |
| UI 框架 | QMainWindow + QDockWidget + QSplitter |
| 样式 | QSS (VS Code 风格暗色主题) |

## 参与贡献

1. Fork 本仓库
2. 新建 `Feat_xxx` 分支
3. 提交代码
4. 新建 Pull Request

## 作者

**蔡可杰 (Jake.cai)**

- GitHub: [https://github.com/JakeCai](https://github.com/JakeCai)
- 项目地址: [https://github.com/JakeCai/openbus](https://github.com/JakeCai/openbus)
- 邮箱: 929168503@qq.com

## 商业合作

如需商业授权、定制开发、技术支持或业务合作，请通过以下方式联系：

- **邮箱**: 929168503@qq.com
- **微信**: 13368295840
- **GitHub Issues**: [https://github.com/JakeCai/openbus/issues](https://github.com/JakeCai/openbus/issues)

## 开源协议

本项目基于 [MIT License](LICENSE) 开源，商业使用请联系作者获取授权。

---

## 技术栈演进路线（逐步引入）

**原则**：每引入一个组件都要保证构建成功、功能正常后再引入下一个，避免大规模重构导致系统不稳定。

### ✅ 第一阶段：已实现的核心依赖（v1.0-当前版本）

| 功能模块 | 组件 | 协议 | 集成状态 | 说明 |
|---------|------|------|---------|------|
| **DBC 解析** | [dbcppp](https://github.com/eclipse/dbcppp) | MIT | ✅ 已内嵌源码集成 | 已集成到 `src/core/dbcpppp_includes/`，在 CMakeLists.txt 中直接编译 |
| **图形绘制** | Qt6 Charts | LGPL-3.0 | ✅ 已安装并配置 | 使用标准模块，无需额外依赖 |
| **日志输出** | qDebug | - | ✅ 默认可用 | Qt 内置调试输出 |

---

### 🔄 第二批引入：日志系统与数据结构增强

#### 1. spdlog（高性能 C++ 日志库）

**协议**: MIT  
**GitHub**: https://github.com/gabime/spdlog  
**用途**: 替换 qDebug，提供更强大的日志分级、异步写入、文件轮转、彩色终端支持。

**集成计划**:
- [x] 源码引入 spdlog v1.14.1（header-only 模式）→ `third_party/spdlog/`
- [x] 创建 `src/core/logging.h/cpp` 封装 spdlog API
- [x] 定义宏 `OPENBUS_LOG_DEBUG/INFO/WARN/ERROR`
- [ ] 迁移关键模块（DBC 解析、Trace 过滤、回放引擎）
- [ ] 移除调试代码中的 `qDebug()`

**预期收益**:
- 更细粒度的日志级别控制（DEBUG/INFO/WARN/ERROR/FATAL）
- 日志自动保存到 `logs/sin_YYYYMMDD.log`
- 跨线程安全，避免 UI 阻塞
- 支持日志筛选和动态调整级别

---

#### 2. nlohmann/json（JSON 解析库）

**协议**: MIT  
**GitHub**: https://github.com/nlohmann/json  
**用途**: 项目配置文件、工程文件 (.openbusproj)、UI 布局配置保存与加载。

**集成计划**:
- [ ] 单头文件引入（无需编译）
- [ ] 创建 `src/utils/config.cpp` 序列化/反序列化 API
- [ ] 实现 `.openbusproj` 工程文件 JSON 格式转换（原纯文本解析 → JSON）
- [ ] UI 布局配置 JSON 化（Tab 位置、大小、Dock 窗口状态）

**预期收益**:
- 配置文件可读性提升
- 类型安全的序列化/反序列化
- 更容易扩展新字段（向后兼容）

---

### 📋 第三批引入：数据流与表达式处理

#### 3. moodycamel::ConcurrentQueue（无锁队列）

**协议**: BSD-2-Clause  
**GitHub**: https://github.com/cameron314/concurrentqueue  
**用途**: Trace 接收、回放、模拟三端数据流传递，替代 `QQueue` + `QMutex`。

**集成计划**:
- [x] 单头文件引入 → `third_party/concurrentqueue/`（concurrentqueue.h + blockingconcurrentqueue.h）
- [x] 创建 `src/utils/message_queue.h` 封装 `FrameQueue` 类
- [x] CanSimulator 改为后台线程生成帧 → 无锁队列 → 主线程批量消费
- [ ] 测试高负载下无丢包、零等待时间

**预期收益**:
- 无锁设计，性能比 `QMutex+QQueue` 高 3-5 倍
- 生产者 - 消费者模型天然适合 CAN 报文流
- CPU 占用更低，适合高频报文场景（10k+ Hz）

---

#### 4. 自写过滤表达式引擎（替代 exprtk）

**协议**: MIT（项目自有代码，零外部依赖）
**用途**: Trace 页面「过滤器」输入 `id == 0x50 && dlc > 8` 实时求值。

**实现方案**:
- [x] 自写递归下降解析器（Tokenizer → Parser → AST → Evaluator）
- [x] 支持变量: `id, dlc, ch, time, fd, ext, rx, tx, std`
- [x] 支持运算符: `== != > < >= <= && || !`
- [x] 支持语法糖: 裸十六进制 (`0x123` → `id==0x123`)、`id in`、`data contains`
- [x] 集成到 `FilterEngine`（pimpl 模式封装）

**收益**:
- 零外部依赖，编译极快（exprtk 头文件 ~1MB 导致链接超时）
- 完全掌控语法和错误提示
- 无协议风险（exprtk 为 LGPL/GPL，商用受限）

---

### 🎨 第四批引入：绘图与可视化增强

#### 5. QCustomPlot（Qt 图表库）

**协议**: GPL-2.0 / Commercial  ⚠️ **注意商用协议**
**官方站点**: https://www.qcustomplot.com/  
**用途**: 替代 Qt6Charts，提供更高的可定制性和丰富的图表面板（波形图、频谱图、示波器等）。

**集成计划**:
- [ ] 下载源码 `qcustomplot.h/cpp`
- [ ] 集成到 `src/ui/charts/qcustomplot/`
- [ ] 评估 GPL 协议：如用于商业软件需购买许可证（~€300）
- [ ] 如不想付费：可继续使用 Qt6Charts，或改用 Apache-2.0 协议的 `ScottPlot.Cpp`（新兴）
- [ ] 创建 `GraphicViewV2` 继承 `QCustomPlot`，保留现有 `Signal` 接口兼容

**预期收益**:
- 更美观的波形渲染效果
- 支持缩放、平移、多 Y 轴、光标读数等高级交互
- 可轻松添加网格线、图例、轴标签

**⚠️ 风险提示**: 若不想购买 GPL 商业许可，请跳过 QCustomPlot，改用以下免费选项：
- **替代 1**: 自定义 `QWidget` 绘制（推荐长期维护）
- **替代 2**: `ScottPlot.Cpp`（Apache-2.0，新兴库）

---

#### 6. FastTable / QtAdvancedTableView（高性能表格）

**协议**: LGPL / Commercial  
**GitHub**: https://github.com/fastfloat/fast-table  
**用途**: Trace 页面大量报文数据的快速展示（万行级流畅滚动）。

**集成计划**:
- [ ] 评估 FastTable 是否满足需求（主要是性能和可扩展性）
- [ ] 当前 `QTableWidget` 在 1 万行以下表现良好，暂不急进
- [ ] 如后续发现性能瓶颈，再考虑替换

---

### 🌳 第五批引入：树形控件增强

#### 7. QTreeWidgetEx（树形控件增强）

**协议**: LGPL-2.1  
**GitHub**: https://github.com/JoseMaQue/QTreeWidgetEx  
**用途**: DBC 详情标签页的信号浏览器，支持分组、折叠、筛选、搜索信号。

**集成计划**:
- [ ] 下载源码并集成
- [ ] 替换 `DbcDetailTab` 内部左侧的 `QTreeWidget` 为 `QTreeWidgetEx`
- [ ] 增加「按报文 ID 分组」「按收发节点分类」「关键字高亮」等功能

---

### 🪟 第六批引入：高级窗口管理

#### 8. Qt Advanced Docking System (QtADS)

**协议**: MIT  
**GitHub**: https://github.com/aqtads/qtads  
**用途**: 拖拽拆分、浮动子窗口、标签化面板、一键保存/加载界面布局。

**集成计划**:
- [ ] 评估 QtADS 是否能稳定工作（文档有限）
- [ ] 创建 `MainWindow::saveLayout()` / `loadLayout()` 基于 QtADS
- [ ] 支持用户自定义 Tab 布局（如：左侧 DBC 详情 + 右侧 Trace + 上部波形图）
- [ ] 退出时自动保存布局，下次启动恢复

**预期收益**:
- VS Code 级别的自由布局体验
- 用户可以分屏同时看 Trace、Graphic、DBC 详情
- 支持多显示器拖动独立窗口

---

### 🧩 可选扩展：脚本语言嵌入

#### 9. sol2 + Lua / pybind11 + Python（二选一）

**用途**: 允许用户使用脚本编写自动化测试用例、自定义数据处理逻辑。

**集成计划**:
- [ ] Lua（轻量）: sol2 + lua.hpp（单头文件）
- [ ] Python（强大但重量）: pybind11 + 独立 Python 解释器 DLL
- [ ] 创建 `src/utils/script_engine.h` 提供统一的脚本执行接口
- [ ] 示例功能：
  ```lua
  -- 自动触发条件
  if signal("DoorLock") == "Locked" then
     send(0x123, "ButtonPress", 1)
  end
  ```

**优先级**: 低，适合后期扩展

---

### 📦 其他硬件相关库

| 功能 | 推荐方案 | 备注 |
|------|---------|------|
| **SocketCAN (Linux)** | libsocketcan (MIT) | Linux 原生支持，`can-utils` 已打包 |
| **Kvaser USB 设备** | Kvaser C API SDK (商用) | 需购买授权，Windows/Linux/macOS |
| **PEAK-Systems PCAN** | PEAK SDK (商用) | 需购买授权，仅 Windows/Linux |
| **BLF/ASC/CSV/PCAP/TRC 日志** | 自研解析 | ✅ 已实现，支持读写 BLF/ASC/CSV，读取 PCAP/TRC |

**集成顺序建议**:
1. 先用内置模拟器完成所有逻辑开发
2. 优先集成 **Kvaser**（中国市场占有率最高）
3. 再根据用户需求补充 SocketCAN / PEAK

---

## 总结：渐进式演进策略

✅ **已完成**: DBC 解析（dbcppp）、图形绘制（Qt6Charts）  
🔄 **近期目标** (1-2 周):
   - 引入 spdlog（日志系统）
   - 引入 nlohmann/json（配置管理）
   
📅 **中期目标** (1-2 月):
   - 引入 moodycamel::ConcurrentQueue（高性能消息队列）
   - 引入 exprtk/re2（表达式过滤）
   - 评估 QCustomPlot（替换或升级绘图库）
   - 引入 QtADS（高级窗口管理）

🚀 **长期规划**:
   - 引入 Lua/Python（脚本扩展）
   - 支持真实 CAN 硬件（Kvaser/PEAK/SocketCAN）
   - BLF/ASC/CSV/PCAP/TRC 报文文件分析（✅ 已实现）

💡 **核心原则**：每次只改一个模块，确保构建通过、功能正确、回归测试无误，再继续下一步.

---

## 整体架构设计

> 对标 CANoe 中心化架构，融合 Wireshark 优势；目标支持 CAN/CAN FD/EtherCAT，具备 Trace、Graphic、报文录制回放、DBC 解析、多总线时间对齐；技术底座：Qt6 + C++17，商用友好开源组件。

### 总体架构思想

**中心化发布订阅架构**：全局唯一数据流中心（DataCore），所有数据源汇入中心，统一时间处理后分发至各个业务模块；模块之间零直接依赖。

分层自上而下：UI 交互层 → 业务服务层 → 核心数据内核层 → 硬件/文件抽象层

```
┌─────────────────────────────────────────────────────┐
│ UI 交互层（Qt）                                       │
│ Trace 窗口｜Graphic 曲线｜信号树｜十六进制面板｜命令行｜AI 侧边栏│
│ 依赖组件：Qt Advanced Docking System、QCustomPlot、QHexEdit2│
└───────────────────────────┬─────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────┐
│ 业务服务层（松耦合服务，订阅 DataCore 数据）             │
│ 过滤服务｜统计服务｜DBC 协议解析服务｜日志服务｜回放服务    │
└───────────────────────────┬─────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────┐
│ 核心内核层 DataCore（对标 CANoe Measurement Core）     │
│ 统一时间对齐模块｜报文分发器｜发布订阅管理器｜数据缓存      │
└───────────────────────────┬─────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────┐
│ 设备抽象层 HAL（对标 CANoe VTI）                       │
│ 采集硬件适配器｜离线日志适配器｜仿真报文源                  │
└─────────────────────────────────────────────────────┘
```

### 各层级详细设计

#### 一、设备抽象层 HAL

**目标**：隔离硬件设备与上层业务，实现在线采集 / 离线回放 / 仿真模式无缝切换

1. **统一抽象接口 `IBusSource`**
   - 开启/关闭通道
   - 报文接收回调
   - 发送报文接口

2. **三类实现**
   - **硬件采集适配器**：对接自研 Zynq 采集器 USB 数据流，接收带硬件时间戳的 CAN/CAN FD/EtherCAT 原始报文
   - **离线文件适配器**：加载 BLF/ASC/自定义录制文件，读取报文时序
   - **仿真源适配器**：软件内部生成测试报文

3. **输出标准统一报文结构体**

```cpp
struct BusMessage {
    uint64_t timestamp_ns;   // 统一纳秒时间戳
    uint8_t bus_type;        // CAN/CAN FD/ETHERCAT
    uint8_t channel;
    uint32_t id;
    std::vector<uint8_t> data;
    bool is_error_frame;
    // 原始协议信息
};
```

#### 二、核心内核层 DataCore【整个软件心脏】

1. **时间对齐模块**（差异化重点，CANoe 短板）
   - 硬件报文携带硬件时间戳优先使用
   - 多总线（CAN + EtherCAT）时间偏移校准，形成全局唯一时间坐标系
   - 离线回放严格按照原始时间戳调度

2. **报文分发中心**（发布订阅模型）
   - HAL 向上推送报文 → DataCore 完成时间标准化 → 分发给所有订阅服务
   - 订阅者：日志服务、统计服务、UI 数据代理、过滤服务

3. **两级缓存**
   - **环形实时缓存**：供给 Trace 实时展示
   - **持久化缓存**：交由日志服务落盘

**关键设计**：UI 缓存与持久化存储解耦，界面卡顿不会造成录制丢帧（复刻 CANoe 优势）

#### 三、业务服务层（独立后台服务，Qt 多线程运行）

全部运行在后台工作线程，禁止直接操作 UI

1. **DBC 解析服务**（全局单例）
   - 加载 DBC，提供报文→信号双向转换；所有 UI 模块共享解析实例
   - 依赖：dbcppp

2. **日志录制服务**
   - 支持持续录制、条件触发录制、文件分片；写入自定义二进制格式 + 兼容 BLF 导入导出

3. **回放服务**
   - 读取日志文件，匀速/倍速/单步推送报文至 DataCore；在线、回放共用一套上层逻辑

4. **高级过滤服务**
   - 两层过滤：后台二进制预过滤（减少数据流转）+ 上层类 Wireshark 表达式过滤器

5. **总线统计服务**
   - 总线负载、报文周期、抖动统计、错误帧统计

#### 四、UI 交互层（Qt6）

所有 UI 组件只订阅业务服务推送的数据，不直接访问底层硬件

**基础依赖组件**：

1. **Qt Advanced Docking System**：自由拖拽分栏、多标签、自定义布局（对标 VS Code）
2. **QCustomPlot**：Graphic 信号曲线，实现多坐标轴、游标、同步时间轴
3. **虚拟 Table Model**：Trace 报文列表，百万行流畅滚动（禁止 QTableWidget）
4. **QHexEdit2**：原始报文十六进制查看
5. **QSortFilterProxyModel 扩展**：表格实时筛选

**UI 模块清单**：

1. **Trace 窗口**：报文列表、行着色、标记点、检索、详情面板
2. **Graphic 窗口**：曲线绘图；支持信号拖拽创建曲线，多窗口时间游标联动
3. **SignalTree 信号浏览器**：DBC 报文、信号树形结构
4. **十六进制详情面板**
5. **底部命令行窗口**：输入过滤表达式，历史记录
6. **AI 辅助侧边栏**：信号分析、异常识别（可选扩展）

#### 五、线程模型设计（规避 Qt 常见坑）

1. **Qt 主线程**：只负责 UI 渲染
2. **DataCore 独立线程**：报文接收、分发、时间处理（高优先级）
3. **日志写入独立线程**：磁盘 IO 不阻塞数据流
4. **回放引擎独立线程**

✅ **规则**：所有跨线程数据传递使用 `Qt::QueuedConnection` + 只读报文结构体，避免内存竞争；大量报文采用对象池减少内存频繁申请释放。

#### 六、数据流两条核心路径

**路径 1：硬件在线采集**

```
采集硬件 → HAL 适配器 → DataCore（时间统一）
→ 分发：
  → 日志服务（持久存储）
  → 过滤服务 → Trace UI
  → DBC 解析服务 → Graphic 信号曲线
  → 统计服务
```

**路径 2：离线日志回放**

```
日志文件 → 回放服务 → DataCore
后续分发链路和在线模式完全一致
```

**巨大优势**：上层 UI 不需要区分在线/离线，大幅减少重复代码（借鉴 CANoe 经典设计）

#### 七、与 CANoe 架构对比 & 差异化设计

✅ **继承 CANoe 优秀点**

1. 中心化数据流，在线/离线逻辑复用
2. 持久化录制与界面解耦，防止界面卡顿丢数据
3. 全局统一信号数据库（DBC）

✅ **新增差异化**（弥补 CANoe 短板）

1. 原生支持 CAN FD + EtherCAT 多总线全局时间对齐
2. 引入 Wireshark 风格表达式过滤引擎
3. VS Code 式自由可停靠布局
4. 预留 AI 分析扩展接口
5. 架构不绑定 Windows，Qt 天然支持后续移植 Linux

#### 八、风险与工程优化要点

1. **海量报文性能**：Trace 必须使用虚拟 Model；曲线控件实现视口降采样
2. **内存控制**：采用环形缓冲区，限制最大缓存报文数量，防止内存持续上涨
3. **协议扩展**：`IBusMessage` 统一结构体，后续新增总线协议无需重构上层 UI
4. **授权风险**：组件优先选用 MIT/BSD 协议（QADS、QHexEdit2），谨慎使用 GPL 组件

#### 九、插件系统架构设计（v2 重构方案）

> 参考 VS Code Extension API 设计思想，主进程（Qt C++）与插件进程（Python + PyQt6）完全隔离，双通道通信实现高效 raw data 交互，订阅制按需推送。

##### 1. 设计目标

| 目标 | 说明 |
|------|------|
| **进程隔离** | 每个插件运行在独立 Python 进程中，插件崩溃不影响主程序和其他插件 |
| **高效数据传输** | 二进制协议替代 JSON，CAN 帧批量传输，吞吐量 ≥ 100k frames/s |
| **订阅制推送** | 插件按需订阅（CAN ID / 通道 / 信号），主程序仅推送匹配数据，无订阅 = 零开销 |
| **完整 SDK** | Python SDK 封装全部通信细节，插件开发者只需关注业务逻辑 |
| **VS Code 风格** | 激活事件、贡献点、命令注册、独立 UI 窗口，与 VS Code 扩展模型对齐 |
| **声明式贡献点** | 插件通过 `plugin.json` 声明式描述能力（命令/视图/设置/数据输入输出/协议解码器），主程序解析后即注册 UI 和数据路由，无需激活插件进程即可发现其全部能力 |
| **零侵入主程序** | 插件系统作为纯消费者连接到现有信号与单例，不修改任何现有源文件；集成点仅为一行 `PluginManager::instance()->initialize()` |
| **协议契约稳定** | 协议版本化（major.minor），仅增量演进——可新增消息类型/方法/字段，禁止修改已定义语义或二进制编码，禁止删除已发布接口；前后向兼容，旧插件与新主程序互操作 |

##### 2. 进程模型

```
┌──────────────────────────────────────────────────────────────┐
│ 主进程 openbus.exe (Qt C++)                                    │
│                                                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐          │
│  │ TraceView   │  │ GraphicView │  │ MainWindow  │          │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘          │
│         │                │                │                  │
│  ┌──────┴─────────────────┴────────────────┴──────┐          │
│  │          PluginManager (C++ 单例)                │          │
│  │  · 发现插件 · 激活/停用 · 订阅路由 · 崩溃重启      │          │
│  └──┬───────────┬───────────┬───────────┬─────────┘          │
│     │           │           │           │                    │
│  ┌──┴──┐    ┌──┴──┐    ┌──┴──┐    ┌──┴──┐                 │
│  │Host1│    │Host2│    │Host3│    │HostN│  PluginHost(C++)  │
│  └──┬──┘    └──┬──┘    └──┬──┘    └──┬──┘  每插件一个进程    │
│     │           │           │           │                    │
└─────┼───────────┼───────────┼───────────┼────────────────────┘
      │ Named Pipe │           │           │
      │ + Binary   │           │           │
      ▼           ▼           ▼           ▼
┌───────────┐ ┌───────────┐ ┌───────────┐ ┌───────────┐
│ Plugin A  │ │ Plugin B  │ │ Plugin C  │ │ Plugin N  │
│ Python    │ │ Python    │ │ Python    │ │ Python    │
│ PyQt6     │ │ PyQt6     │ │ 无UI      │ │ PyQt6     │
│ openbus   │ │ openbus   │ │ openbus   │ │ openbus   │
│ SDK       │ │ SDK       │ │ SDK       │ │ SDK       │
└───────────┘ └───────────┘ └───────────┘ └───────────┘
```

**关键设计决策**：

- **每插件一进程**：`PluginHost` (C++) 通过 `QProcess` 为每个已激活插件启动独立 Python 进程，彻底隔离插件间影响
- **主从隔离**：主进程 C++ Qt6，插件进程 Python + PyQt6，通过 Named Pipe 通信，无共享内存竞争
- **崩溃自愈**：`PluginHost` 监控子进程状态，崩溃后指数退避重启（最多 3 次），超过则标记为「错误」状态
- **资源回收**：插件进程退出时，主程序自动清理该插件的所有订阅、命令注册、UI 窗口

##### 3. 双通道通信架构

```
┌──────────────────────┐          ┌──────────────────────┐
│   主进程 (C++ Qt)     │          │  插件进程 (Python)    │
│                      │          │                      │
│  ┌────────────────┐  │          │  ┌────────────────┐  │
│  │ Control Channel│◄─┼──────────┼─►│ Control Channel│  │
│  │ JSON-RPC 2.0   │  │  Named   │  │ JSON-RPC 2.0   │  │
│  │ 命令/查询/配置   │  │  Pipe    │  │ 命令/查询/配置   │  │
│  └────────────────┘  │ (Text)   │  └────────────────┘  │
│                      │          │                      │
│  ┌────────────────┐  │          │  ┌────────────────┐  │
│  │  Data Channel   │◄─┼──────────┼─►│  Data Channel   │  │
│  │ Binary Protocol │  │  Named   │  │ Binary Protocol │  │
│  │ 帧批量/信号更新   │  │  Pipe    │  │ 帧批量/信号更新   │  │
│  └────────────────┘  │ (Binary) │  └────────────────┘  │
└──────────────────────┘          └──────────────────────┘
```

**为什么双通道？**

| 通道 | 协议 | 用途 | 吞吐量 |
|------|------|------|--------|
| Control | JSON-RPC 2.0 (UTF-8 text) | 激活/停用、命令注册、查询选中帧、DBC 解码请求、配置读写 | 低频，延迟敏感 |
| Data | Binary (自定义二进制协议) | CAN 帧批量推送、信号值更新、统计推送 | 高频，吞吐敏感 |

Named Pipe 在 Windows 上支持全双工、二进制模式，单管道吞吐量 > 200 MB/s，满足 CAN FD 最高速率需求。

##### 4. 二进制数据协议

###### 4.1 消息包格式

所有 Data Channel 消息使用统一的二进制包格式：

```
┌──────────┬──────────┬──────────┬──────────────────────┐
│ Magic    │ MsgType  │ Payload  │ Payload Data         │
│ 4 bytes  │ 2 bytes  │ Length   │ N bytes              │
│ 0x4F425553│ uint16   │ 4 bytes  │                      │
│ "OBUS"   │          │ uint32   │                      │
└──────────┴──────────┴──────────┴──────────────────────┘
  固定头 10 bytes              变长 payload
```

###### 4.2 消息类型

| MsgType | 名称 | 方向 | Payload |
|---------|------|------|---------|
| 0x0000 | _保留（协议保留值，不使用）_ | — | — |
| 0x0001 | `FRAME_BATCH` | 主→插件 | N 个 CAN 帧的二进制打包 |
| 0x0002 | `SIGNAL_UPDATE` | 主→插件 | 信号名+值+时间戳的批量打包 |
| 0x0003 | `STATISTICS` | 主→插件 | 总线统计快照 |
| 0x0010 | `SUBSCRIBE` | 插件→主 | JSON 订阅请求 |
| 0x0011 | `UNSUBSCRIBE` | 插件→主 | JSON 取消订阅 |
| 0x0020 | `RPC_REQUEST` | 双向 | JSON-RPC 2.0 请求 |
| 0x0021 | `RPC_RESPONSE` | 双向 | JSON-RPC 2.0 响应 |
| 0x0022 | `RPC_NOTIFICATION` | 双向 | JSON-RPC 2.0 通知 |

###### 4.3 CAN 帧二进制编码

单帧编码（16 + N 字节，N = 实际数据长度 0-64）：

```
Offset  Size  Field         说明
0       4     can_id        CAN ID (bit31=扩展帧, bit30=FD, bit29=BRS, bit28=ESI)
4       1     flags         bit7=extended, bit6=fd, bit5=brs, bit4=esi, bit3=direction(0=Rx,1=Tx)
5       1     dlc           数据长度码 0-15
6       1     channel       通道号 (1-based)
7       1     data_length   实际数据字节数 (0-64)
8       8     timestamp_ns  纳秒时间戳 (相对起点)
16      N     data          原始数据 (N = data_length)
```

**批量帧编码**：`[uint16 frame_count][frame_1][frame_2]...[frame_N]`

**效率对比**（100 帧 Classic CAN，每帧 8 字节数据）：

| 编码方式 | 单帧大小 | 100 帧总大小 | 倍率 |
|---------|---------|------------|------|
| JSON-RPC (hex) | ~220 bytes | ~22,000 bytes | 1× (基准) |
| 二进制协议 | 24 bytes | 2,402 bytes | **9.2× 压缩** |

###### 4.4 Control Channel — JSON-RPC 2.0 方法清单

**主程序 → 插件**：

| Method | 说明 |
|--------|------|
| `activate` | 激活插件，传入 context 参数 |
| `deactivate` | 停用插件 |
| `executeCommand` | 执行插件注册的命令 |
| `fileOpened` | 通知文件打开事件 |
| `shutdown` | 优雅关闭 |

**插件 → 主程序**：

| Method | 说明 |
|--------|------|
| `output.append` | 追加文本到输出面板 |
| `output.clear` | 清空输出面板 |
| `sendFrame` | 请求发送 CAN 帧 |
| `registerCommand` | 注册命令到主程序菜单 |
| `frames.getSelected` | 查询 Trace 中选中的帧 |
| `frames.getRecent` | 查询最近 N 帧 |
| `signals.decode` | 请求 DBC 信号解码 |
| `signals.encode` | 请求 DBC 信号编码 |
| `workspace.getProjectDir` | 查询当前工程目录 |
| `workspace.getDbcFiles` | 查询已加载 DBC 文件列表 |
| `workspace.getSetting` | 读取应用设置 |
| `statusbar.set` | 设置状态栏文本 |
| `log` | 插件日志 |

> **双向通知**（连接建立后立即交换）

| Method | 方向 | 说明 |
|--------|------|------|
| `hello` | 双向 | 协议握手，交换 `{protocolVersion, sdkVersion, capabilities, supportedMethods}` |

###### 4.5 协议稳定性契约

协议是主程序与插件之间的**永久契约**，一经发布即冻结语义。以下规则确保插件一次编写、跨版本运行：

**版本协商**

```
连接建立
  │
  ├─ 插件 → 主: hello { protocolVersion: "1.0", sdkVersion: "1.2.3", capabilities: [...] }
  ├─ 主 → 插件: hello { protocolVersion: "1.0", appVersion: "3.5.0", capabilities: [...] }
  │
  ├─ major 版本相同? ── YES → 接受连接，双方取各自 minor 的较小值工作
  │                  ── NO  → 主发送 shutdown，终止连接
  │
  └─ 握手完成，后续通信使用双方均支持的方法集合
```

**兼容性规则**

| 规则 | 说明 |
|------|------|
| **major 版本必须匹配** | major 变更 = 不兼容断代，旧插件无法运行（需迁移） |
| **minor 向前兼容** | 新主程序接受旧插件（缺少的字段使用默认值）；新插件接受旧主程序（不调用不存在的方法） |
| **未知消息类型静默丢弃** | Data Channel 收到未知 MsgType 的包，记录警告后丢弃，不中断连接 |
| **未知 JSON 字段忽略** | Control Channel 收到未知字段，直接忽略，不报错 |
| **能力协商** | `hello` 中的 `capabilities` 数组声明支持的功能域；调用未声明的能力返回 `methodNotFound` 错误而非崩溃 |

**演进策略**

| 操作 | 允许? | 说明 |
|------|-------|------|
| 新增 MsgType | ✅ | 在保留区间分配新 ID（0x0100+ 为 v2 预留） |
| 新增 JSON-RPC 方法 | ✅ | 旧端忽略未知方法通知；请求式方法返回 `methodNotFound` |
| 新增可选字段 | ✅ | 新字段必须可选、有默认值；旧端忽略未知字段 |
| 修改已有消息的二进制编码 | ❌ 永久禁止 | 二进制布局一经发布冻结，如需变更必须使用新 MsgType |
| 修改已有方法的参数语义 | ❌ 永久禁止 | 参数名/类型/含义一经发布冻结 |
| 删除已发布消息类型或方法 | ❌ 永久禁止 | 仅允许标记 `deprecated`，功能保留至 major 版本断代 |
| 新增 MsgType 编码格式 | ✅ | 新 MsgType 可使用任意编码（如 protobuf），不影响已有 MsgType |

**版本区间分配**

```
0x0000        保留
0x0001-0x00FF  v1 消息（当前定义，永久冻结）
0x0100-0x01FF  v2 预留（未来扩展）
0x0200-0x7FFF  未分配
0x8000-0xFFFF  私有/实验区（插件可自定义，不保证互操作）
```

**协议版本与 SDK 版本的关系**

- `protocolVersion`（如 `"1.0"`）= 通信契约版本，极少变更（仅断代时 major+1）
- `sdkVersion`（如 `"1.2.3"`）= SDK 实现版本，常规迭代
- 插件开发者只需关注 `protocolVersion`；SDK 封装所有版本协商细节

##### 5. 订阅制数据推送

###### 5.1 订阅模型

插件通过 SDK 订阅感兴趣的数据流，主程序仅推送匹配的数据：

```python
import openbus

def activate(context: openbus.ExtensionContext):
    # 订阅特定 CAN ID 的帧
    context.frames.subscribe(
        ids=[0x123, 0x456, 0x7FF],   # 仅这些 CAN ID（省略 = 全部）
        channels=[1],                 # 仅通道 1（省略 = 全部通道）
        direction="Rx",               # 仅 Rx 帧（省略 = 全部）
        handler=on_frame              # 回调函数
    )

    # 订阅 DBC 信号实时值
    context.signals.subscribe(
        names=["EngineRPM", "VehicleSpeed"],
        handler=on_signal             # (name, value, timestamp)
    )

    # 订阅总线统计（1 秒间隔）
    context.statistics.subscribe(
        interval_ms=1000,
        handler=on_stats              # (stats_dict)
    )

def on_frame(frame: openbus.CanFrame):
    print(f"0x{frame.id:X} ch={frame.channel} data={frame.data.hex()}")

def on_signal(name: str, value: float, timestamp: float):
    print(f"{name} = {value:.2f} @ {timestamp:.3f}s")

def on_stats(stats: dict):
    print(f"负载率: {stats['bus_load_percent']:.1f}%  总帧数: {stats['total_frames']}")
```

###### 5.2 订阅路由表

主程序为每个插件进程维护一张订阅表：

```
Plugin: "j1939-analyzer" (PID 12345)
┌──────────┬─────────────────────────────────┬─────────┐
│ 类型     │ 过滤条件                         │ 状态     │
├──────────┼─────────────────────────────────┼─────────┤
│ frames   │ ids=[0x18FEF100,0x18EEFF00]     │ active  │
│          │ channels=[1]                     │         │
│ signals  │ names=["EngineRPM","OilTemp"]    │ active  │
│ stats    │ interval_ms=1000                 │ active  │
└──────────┴─────────────────────────────────┴─────────┘
```

> 上表中 `frames` 和 `signals` 订阅来自 `plugin.json` 的 `dataInputs` 声明，
> 在插件激活时自动创建；插件也可在 `activate()` 中追加动态订阅。

当 CAN 帧到达时：
1. 主程序遍历所有插件进程的订阅表
2. 对每个订阅执行 ID/通道/方向匹配
3. 匹配的帧打包为 `FRAME_BATCH` 二进制包，通过 Data Channel 推送
4. 无匹配订阅的插件进程：零开销，不触发任何 I/O

###### 5.3 节流与批处理

- **帧批量**：主程序每 10ms（可配置）将匹配的帧打包成一批，减少 IPC 次数
- **最大批大小**：每批不超过 1024 帧（避免大包阻塞管道）
- **信号去重**：同一信号在 50ms 内只推送最新值（避免高频信号淹没插件）
- **声明式处理模式**：`dataInputs` 中的 `processing` 字段控制推送策略——`streaming` 即时推送，`batch` 按 10ms 批量打包（见 §7.4）

##### 6. Python SDK 设计 (`openbus` 包)

###### 6.1 包结构

```
sdk/openbus/
├── __init__.py           # 公共 API 导出
├── _transport.py         # Named Pipe 传输层（内部）
├── _binary.py            # 二进制协议编解码（内部）
├── context.py            # ExtensionContext — 插件上下文
├── frames.py             # CanFrame 类 + 帧订阅/发送 API
├── signals.py            # 信号解码/编码 + 信号订阅 API
├── output.py             # 输出面板 API
├── commands.py           # 命令注册/执行 API
├── workspace.py          # 工程上下文查询 API
├── statistics.py         # 总线统计订阅 API
├── statusbar.py          # 状态栏 API
└── ui.py                 # 独立窗口创建 API (PyQt6)
```

###### 6.2 核心 API

```python
import openbus

# ====== CanFrame 数据类 ======
class CanFrame:
    id: int              # CAN ID
    extended: bool       # 扩展帧
    fd: bool             # CAN FD
    dlc: int             # 数据长度码
    data: bytes          # 原始数据 (0-64 bytes)
    timestamp: float     # 相对时间戳 (秒)
    channel: int         # 通道号
    direction: str       # "Rx" 或 "Tx"
    bitrate_switch: bool # BRS (CAN FD)
    error_state: bool    # ESI (CAN FD)

# ====== ExtensionContext ======
class ExtensionContext:
    """插件激活时收到的上下文对象，所有 API 的入口"""

    # 子系统
    frames: FramesAPI         # 帧订阅与发送
    signals: SignalsAPI       # 信号编解码与订阅
    output: OutputAPI         # 输出面板
    commands: CommandsAPI     # 命令注册
    workspace: WorkspaceAPI   # 工程上下文
    statistics: StatisticsAPI # 总线统计
    statusbar: StatusbarAPI   # 状态栏
    ui: UIAPI                 # 独立窗口 (需 PyQt6)

    # 生命周期
    plugin_name: str          # 当前插件名

# ====== FramesAPI ======
class FramesAPI:
    def subscribe(self, *, ids=None, channels=None,
                  direction=None, handler=None) -> int:
        """订阅 CAN 帧，返回 subscription_id"""

    def unsubscribe(self, sub_id: int) -> None:
        """取消订阅"""

    def get_selected(self) -> list[CanFrame]:
        """查询 Trace 中选中的帧"""

    def get_recent(self, count: int = 100) -> list[CanFrame]:
        """查询最近 N 帧"""

    def send(self, id: int, data: bytes, *,
             extended: bool = False, fd: bool = False) -> None:
        """发送 CAN 帧"""

# ====== SignalsAPI ======
class SignalsAPI:
    def decode(self, can_id: int, data: bytes) -> dict[str, float]:
        """解码信号值"""

    def encode(self, can_id: int, values: dict[str, float]) -> bytes:
        """编码信号值"""

    def subscribe(self, names: list[str], handler) -> int:
        """订阅信号实时更新，返回 subscription_id"""

    def unsubscribe(self, sub_id: int) -> None:
        """取消订阅"""

# ====== UIAPI (需 PyQt6) ======
class UIAPI:
    def create_window(self, title: str = "") -> QMainWindow:
        """创建独立窗口，关闭时自动通知主程序"""

    def show_message(self, title: str, text: str) -> None:
        """信息对话框"""

    def show_warning(self, title: str, text: str) -> None:
        """警告对话框"""

    def show_error(self, title: str, text: str) -> None:
        """错误对话框"""
```

###### 6.3 插件入口约定

```python
# main.py — 插件入口
import openbus

def activate(context: openbus.ExtensionContext):
    """插件激活时调用。

    此时 plugin.json 中声明的 dataInputs 已自动生效（订阅已创建），
    commands / views / configuration 已由主程序注册。
    activate() 中只需处理动态逻辑和运行时追加的订阅/命令。
    """
    context.output.append("插件已加载")

    # 声明式 dataInputs 已自动订阅，此处仅追加动态订阅
    context.frames.subscribe(ids=[0x123], handler=on_frame)

    # 声明式 commands 已注册，此处仅追加动态命令
    context.commands.register("myPlugin.dynamicAction", do_action, "动态操作")

def on_frame(frame: openbus.CanFrame):
    pass

def do_action():
    pass

def deactivate():
    """插件停用时调用（可选）。

    主程序自动清理声明式订阅和注册项；
    动态创建的订阅/命令也由 SDK 自动注销。
    """
    pass
```

> **声明式优先原则**：`plugin.json` 中能声明的能力（固定订阅、命令、设置项、
> 视图、文件格式等）不应在 `activate()` 中重复注册。`activate()` 仅处理
> 需要运行时上下文的动态逻辑（如根据 DBC 内容决定订阅哪些信号）。

##### 7. 插件清单文件 (`plugin.json`) — 声明式贡献点系统

> 参考 VS Code `package.json` 的 `contributes` 机制，插件通过声明式清单描述自身能力、数据输入输出与关键逻辑。
> 主程序解析清单后即可注册 UI 元素、预建数据路由表、渲染设置页，**无需激活插件进程**。
> 声明式清单是零侵入原则的核心使能器——主程序不调用插件即可发现其全部能力。

###### 7.1 完整示例

```json
{
    "name": "j1939-analyzer",
    "displayName": "J1939 协议分析器",
    "version": "1.0.0",
    "author": "Example Corp",
    "description": "J1939 协议解析、诊断与报文生成",
    "main": "main.py",
    "minProtocolVersion": "1.0",

    "activationEvents": [
        "onCommand:j1939.analyze",
        "onFile:.j1939",
        "onSignal:EngineRPM",
        "onStartup"
    ],

    "contributes": {
        "commands": [
            {
                "id": "j1939.analyze",
                "title": "J1939: 分析报文",
                "category": "J1939",
                "icon": "resources/analyze.svg",
                "enablement": "frameCount > 0"
            },
            {
                "id": "j1939.generate",
                "title": "J1939: 生成测试报文",
                "category": "J1939",
                "icon": "resources/generate.svg"
            }
        ],

        "menus": {
            "commandPalette": [
                { "command": "j1939.analyze" },
                { "command": "j1939.generate" }
            ],
            "trace/contextMenu": [
                { "command": "j1939.analyze", "when": "selectedFrame != null", "group": "protocol@1" }
            ],
            "toolbar/main": [
                { "command": "j1939.generate", "group": "protocol@3", "when": "plugin.active == j1939-analyzer" }
            ]
        },

        "views": {
            "panel/bottom": [
                {
                    "id": "j1939.diagnostics",
                    "title": "J1939 诊断",
                    "type": "webview",
                    "when": "plugin.active == j1939-analyzer"
                }
            ],
            "sidebar/right": [
                {
                    "id": "j1939.network",
                    "title": "J1939 网络",
                    "type": "tree",
                    "icon": "resources/network.svg"
                }
            ]
        },

        "configuration": {
            "title": "J1939 分析器",
            "properties": {
                "j1939.baudrate": {
                    "type": "integer",
                    "default": 500000,
                    "enum": [250000, 500000, 1000000],
                    "enumDescriptions": ["250 kbps", "500 kbps", "1 Mbps"],
                    "description": "J1939 波特率"
                },
                "j1939.sourceAddress": {
                    "type": "integer",
                    "default": 254,
                    "minimum": 0,
                    "maximum": 253,
                    "description": "本节点源地址 (0-253, 254=空地址)"
                },
                "j1939.preferredName": {
                    "type": "string",
                    "default": "J1939 Tool",
                    "description": "工具在网络中的名称"
                }
            }
        },

        "dataInputs": [
            {
                "type": "frames",
                "filter": {
                    "ids": [405419776, 416074496],
                    "channels": [1],
                    "direction": "Rx"
                },
                "processing": "streaming"
            },
            {
                "type": "signals",
                "names": ["EngineRPM", "VehicleSpeed", "OilTemp"],
                "processing": "streaming"
            },
            {
                "type": "statistics",
                "interval": 1000
            }
        ],

        "dataOutputs": [
            {
                "type": "signals",
                "signals": [
                    {
                        "name": "J1939.EngineTorque",
                        "unit": "Nm",
                        "min": 0,
                        "max": 5000,
                        "description": "发动机扭矩（由 PGN 61444 计算）"
                    },
                    {
                        "name": "J1939.FuelRate",
                        "unit": "L/h",
                        "min": 0,
                        "max": 500,
                        "description": "瞬时油耗"
                    }
                ]
            },
            {
                "type": "diagnostics",
                "format": "dtc",
                "description": "J1939 DM1 诊断码"
            }
        ],

        "signalDecoders": [
            {
                "name": "J1939 PGN Decoder",
                "protocol": "J1939",
                "match": { "idMask": "0x00FF0000", "idValue": "0x00EE00" },
                "pgns": [
                    { "pgn": 61444, "name": "EEC1", "signals": ["EngineRPM", "EngineTorque"] },
                    { "pgn": 65263, "name": "ET1",  "signals": ["CoolantTemp", "OilTemp"] },
                    { "pgn": 65266, "name": "CC1",  "signals": ["VehicleSpeed"] }
                ]
            }
        ],

        "fileFormats": [
            { "extension": ".j1939", "name": "J1939 日志", "role": "import" },
            { "extension": ".j1939", "name": "J1939 日志导出", "role": "export" }
        ],

        "keybindings": [
            {
                "command": "j1939.analyze",
                "key": "Ctrl+Shift+J",
                "when": "activeView == trace"
            }
        ],

        "statusBar": [
            {
                "id": "j1939.status",
                "text": "J1939: $(check) Online",
                "tooltip": "J1939 插件运行中",
                "alignment": "right",
                "priority": 100,
                "when": "plugin.active == j1939-analyzer"
            }
        ]
    }
}
```

> 上例中 `ids` 使用十进制（如 `405419776` = `0x18FEF100`），因为 JSON 不支持十六进制字面量。
> `match.idMask` / `idValue` 使用字符串形式的十六进制，由 SDK 解析。

###### 7.2 激活事件

| 事件 | 说明 | 示例 |
|------|------|------|
| `onStartup` | 主程序启动时激活 | 后台监控插件 |
| `onCommand:<id>` | 用户执行命令时激活 | `onCommand:j1939.analyze` |
| `onFile:<ext>` | 打开特定扩展名文件时激活 | `onFile:.j1939` |
| `onSignal:<name>` | 指定信号首次出现时激活 | `onSignal:EngineRPM` |
| `onFrame` | 收到首帧时激活 | 帧计数器 |
| `onDbcLoaded` | DBC 文件加载完成时激活 | 信号分析插件 |
| `onView:<id>` | 用户打开插件贡献的视图时激活 | `onView:j1939.diagnostics` |

> 主程序在激活事件触发前仅解析 `plugin.json`，不启动插件进程。
> 声明式贡献的 UI 元素（命令、菜单、设置项）在激活前即可显示和操作；
> 用户触发需要插件处理的操作时，才按需激活（lazy activation）。

###### 7.3 贡献点参考

| 贡献点 | 说明 | 激活前生效 |
|--------|------|:----------:|
| `commands` | 声明插件提供的命令（id + title + icon + enablement） | ✅ |
| `menus` | 声明命令出现在哪些菜单位置 | ✅ |
| `views` | 声明插件贡献的面板/视图（bottom dock / sidebar / tab） | ✅ |
| `configuration` | 声明插件设置项（主程序设置页自动渲染表单） | ✅ |
| `dataInputs` | 声明消费的数据流（主程序预建订阅路由表） | ✅ |
| `dataOutputs` | 声明产出的数据（供其他插件发现和订阅） | ✅ |
| `signalDecoders` | 声明自定义协议解码器（PGN/CANopen/UDS 等） | ✅ |
| `fileFormats` | 声明文件导入/导出能力 | ✅ |
| `keybindings` | 声明快捷键绑定 | ✅ |
| `statusBar` | 声明状态栏项 | ✅ |

> 所有贡献点均在**激活前生效**——主程序解析 `plugin.json` 后立即注册 UI 和路由，
> 用户操作触发激活事件时才启动插件进程。这是零侵入与按需加载的关键。

###### 7.4 数据输入声明 (`dataInputs`)

插件声明式描述消费的数据流，主程序在**激活前预建订阅路由表**，激活后自动推送匹配数据：

| `type` | 说明 | 必填字段 | 可选字段 |
|--------|------|---------|---------|
| `frames` | CAN 帧流 | — | `filter.ids` (数组), `filter.channels` (数组), `filter.direction` ("Rx"/"Tx") |
| `signals` | DBC 信号值流 | `names` (数组) | `processing` ("streaming"/"batch", 默认 streaming) |
| `statistics` | 总线统计快照 | — | `interval` (ms, 默认 1000) |
| `selectedFrame` | Trace 选中帧变化事件 | — | — |

**`processing` 模式**：
- `streaming` — 每帧/每信号即时推送（实时监控类插件）
- `batch` — 主程序按 10ms 批量打包后推送（离线分析类插件，减少 IPC 开销）

**与命令式订阅的关系**：声明式 `dataInputs` 在插件激活时自动创建订阅，等效于在 `activate()` 中调用 `context.frames.subscribe(...)`。插件可在运行时追加动态订阅（如用户交互后订阅新 ID），两者共存互不冲突。

###### 7.5 数据输出声明 (`dataOutputs`)

插件声明式描述产出的数据，使其他插件和主程序可**发现并订阅**这些输出：

| `type` | 说明 | 字段 |
|--------|------|------|
| `signals` | 计算/派生信号 | `signals[].name`, `unit`, `min`, `max`, `description` |
| `diagnostics` | 诊断码 (DTC/SPN) | `format` ("dtc"/"spn"), `description` |
| `log` | 结构化日志 | `level` ("info"/"warning"/"error") |
| `exportData` | 可导出数据 | `format` ("csv"/"json"/"custom"), `description` |

**输出信号注册到全局信号命名空间**：
- 插件输出的信号（如 `J1939.EngineTorque`）注册到主程序信号注册表
- 其他插件可通过 `dataInputs` 声明订阅这些信号，或通过 SDK 命令式订阅
- Graphic 视图可将插件输出信号添加为曲线

**数据流全景**：
```
硬件/回放 → 主程序 → DBC 解码 → 原始信号 ─┬─→ 插件 A (dataInputs: signals)
                                          │      └─→ dataOutputs: signals (J1939.EngineTorque)
                                          │              └─→ 插件 B (dataInputs: signals[J1939.EngineTorque])
                                          │              └─→ Graphic 视图曲线
                                          └─→ 插件 C (dataInputs: frames)
```

###### 7.6 上下文表达式 (`when`)

参考 VS Code 的 `when` 子句，控制 UI 元素的可见性和命令的可用性：

**上下文变量**：

| 变量 | 类型 | 说明 |
|------|------|------|
| `activeView` | string | 当前活动视图 (`"trace"`, `"graphic"`, `"record"`, `"playback"`) |
| `selectedFrame` | object/null | 当前选中的帧（`null` = 未选中） |
| `frameCount` | int | Trace 中帧总数 |
| `recording` | bool | 是否正在录制 |
| `playing` | bool | 是否正在回放 |
| `connected` | bool | 是否连接到硬件设备 |
| `dbcLoaded` | bool | 是否已加载 DBC 文件 |
| `dbcFileCount` | int | 已加载 DBC 文件数 |
| `plugin.active` | string | 当前激活的插件名 |

**运算符**：`==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `!`

**示例**：
- `"when": "selectedFrame != null"` — 选中帧时显示
- `"when": "frameCount > 0 && dbcLoaded"` — 有帧且 DBC 已加载时启用
- `"when": "activeView == trace || activeView == graphic"` — Trace 或 Graphic 视图时显示
- `"when": "plugin.active == j1939-analyzer"` — 本插件已激活时显示

**`enablement` vs `when`**：
- `when` 控制菜单项/视图**是否显示**
- `enablement` 控制命令**是否可执行**（灰色 vs 正常）

###### 7.7 声明式与命令式的关系

```
声明式 (plugin.json)                  命令式 (main.py activate())
┌────────────────────────────┐       ┌────────────────────────────┐
│ dataInputs:                │       │ def activate(ctx):         │
│   - frames [0x18FEF100]    │──激活─▶│   # 声明式订阅已自动生效    │
│   - signals [EngineRPM]    │       │   # 可追加动态订阅:         │
│   - statistics             │       │   ctx.frames.subscribe(    │
│                            │       │     ids=[0xABC], handler=…) │
│ commands: [...]            │       │                            │
│ menus: [...]               │       │   # 声明式命令已注册        │
│ views: [...]               │       │   # 可追加动态命令:         │
│ configuration: [...]       │       │   ctx.commands.register(   │
│ signalDecoders: [...]      │       │     "dynamic.cmd", handler) │
│ dataOutputs: [...]         │       │                            │
└────────────────────────────┘       └────────────────────────────┘
  激活前生效（UI / 路由 / 发现）         激活后生效（运行时 / 动态）
```

| 维度 | 声明式 | 命令式 |
|------|--------|--------|
| 时机 | 激活前（解析 plugin.json） | 激活后（`activate()` 执行） |
| 用途 | 静态能力声明、UI 注册、数据路由 | 动态行为、用户交互、运行时订阅 |
| 示例 | 固定订阅 0x18FEF100 | 用户选择 ID 后动态订阅 |
| 关系 | **基础**——自动创建订阅和注册 UI | **扩展**——在声明式基础上追加动态逻辑 |

> 设计原则：**能声明式完成的，不写命令式代码**。声明式清单是插件与主程序之间的
> 稳定契约的一部分，主程序可跨版本解析；命令式代码可随插件版本自由变化。

##### 8. C++ 侧架构（主程序）

###### 8.0 零侵入集成原则

插件系统作为**纯消费者**接入主程序，不修改任何现有源文件：

```
现有主程序代码（不修改）              插件系统代码（新增模块）
┌─────────────────────────┐        ┌─────────────────────────┐
│ CanSimulator            │        │ PluginManager           │
│   signal: frameReceived │◄───────│   initialize() 中       │
│                         │ connect│   connect 到现有信号     │
├─────────────────────────┤        ├─────────────────────────┤
│ DbcManager (singleton)  │◄───────│ PluginHost              │
│   getSignalValue()      │  call  │   通过单例 API 查询      │
├─────────────────────────┤        ├─────────────────────────┤
│ AppConfig (singleton)   │◄───────│ PluginManager           │
│   value() / setValue()  │  call  │   读写配置              │
└─────────────────────────┘        └─────────────────────────┘
```

**集成点**：仅在 `main.cpp` 或 `MainWindow` 构造函数中增加一行：
```cpp
PluginManager::instance()->initialize();
```

**`initialize()` 内部通过 `connect()` 绑定到现有信号，不改动现有类的任何代码**：
```cpp
void PluginManager::initialize()
{
    // 连接帧数据流 — 来自 CanSimulator / Player / Recorder 已有的信号
    auto *sim = CanSimulator::instance();
    connect(sim, &CanSimulator::frameReceived,
            this, &PluginManager::onFrameReceived);

    // 连接 DBC 信号更新 — 来自 DbcManager 已有的信号
    auto *dbc = DbcManager::instance();
    connect(dbc, &DbcManager::signalUpdated,
            this, &PluginManager::onSignalUpdated);

    // 连接总线统计 — 来自 BusStatistics 已有的信号
    // （BusStatistics::statisticsUpdated 已存在，FrameStatisticsView 已在使用）

    // 扫描插件目录、读取 plugin.json
    discoverPlugins();
}
```

> 如果现有类缺少所需信号，则在插件模块内创建一个**适配层**（Adapter），
> 通过轮询或拦截 QEvent 等非侵入方式获取数据，而非修改现有类。

###### 8.1 类设计

```cpp
// 插件宿主 — 管理单个插件进程的生命周期和通信
class PluginHost : public QObject {
    Q_OBJECT
public:
    bool start(const QString &pluginName, const QString &pluginDir,
               const QString &mainScript, const QString &pythonExe);
    void stop();
    bool isRunning() const;
    qint64 processId() const;

    // Control Channel (JSON-RPC)
    void sendNotification(const QString &method, const QJsonObject &params);
    void sendRequest(const QString &method, const QJsonObject &params,
                     std::function<void(const QJsonValue &)> callback);

    // Data Channel (Binary)
    void sendFrameBatch(const QVector<CanFrame> &frames);
    void sendSignalUpdate(const QString &name, double value, double timestamp);
    void sendStatistics(const BusStatistics::Summary &summary,
                        const QVector<BusStatistics::IdStats> &idStats);

signals:
    void messageReceived(const QString &method, const QJsonObject &params,
                         const QJsonValue &id);
    void hostStarted();
    void hostCrashed();
    void hostError(const QString &error);
};

// 订阅管理 — 每个插件一份
class PluginSubscription {
public:
    struct FrameFilter {
        QSet<quint32> ids;        // 空 = 全部 ID
        QSet<quint8> channels;    // 空 = 全部通道
        bool rxOnly = false;
        bool txOnly = false;
    };

    struct SignalFilter {
        QSet<QString> signalNames;
    };

    bool hasFrameSubscription() const;
    bool matchesFrame(const CanFrame &frame) const;
    bool hasSignalSubscription() const;
    bool matchesSignal(const QString &name) const;

    void addFrameFilter(const FrameFilter &filter);
    void removeFrameFilter(int subId);
    void addSignalFilter(const SignalFilter &filter);
    void removeSignalFilter(int subId);
};

// 插件管理器 — 全局单例
class PluginManager : public QObject {
    Q_OBJECT
public:
    static PluginManager *instance();
    void initialize();
    void shutdown();

    // 由现有信号触发（非主程序直接调用）— 遍历订阅表，仅推送匹配数据
    void onFrameReceived(const CanFrame &frame);

    // 批量帧刷新（10ms 定时器）
    void flushFrameBatches();

    // 协议握手 — 收到插件 hello 后协商版本与能力
    void onPluginHello(const QString &pluginName,
                       const QJsonObject &hello);

private:
    QHash<QString, PluginHost*> m_hosts;           // name → host
    QHash<QString, PluginSubscription> m_subs;     // name → subscriptions
    QHash<QString, QList<CanFrame>> m_frameBuffers; // name → pending frames
};
```

###### 8.2 数据流（帧到达 → 推送到插件）

```
CanSimulator/Player 发出 frameReceived 信号（已有，不修改）
    │
    ▼
PluginManager::onFrameReceived(frame)  ← 由 connect 自动触发
    │
    ├─ 遍历 m_subs (每个已激活插件的订阅表)
    │   │
    │   ├─ matchesFrame(frame)?
    │   │   ├─ YES → 追加到 m_frameBuffers[pluginName]
    │   │   └─ NO  → 跳过（零开销）
    │   │
    │   └─ 下一个插件
    │
    └─ 10ms 定时器触发 flushFrameBatches()
        │
        ├─ 对每个有待发帧的插件:
        │   ├─ 打包为二进制 FRAME_BATCH
        │   ├─ PluginHost::sendFrameBatch(frames)
        │   └─ 清空 buffer
        └─ 完成
```

##### 9. 与现有架构（v1）的对比与迁移

| 维度 | v1（现有） | v2（本方案） |
|------|-----------|-------------|
| 进程模型 | 所有插件共享一个 Python 进程 | **每插件独立进程** |
| 传输 | stdin/stdout JSON-RPC | **Named Pipe 双通道（JSON + Binary）** |
| 数据编码 | JSON hex 字符串 | **二进制协议（9× 压缩）** |
| 推送策略 | 所有 onFrame 插件收全部帧 | **订阅制，按 ID/通道/信号过滤** |
| 帧批量 | 有（JSON 数组） | **二进制批量（10ms 间隔）** |
| SDK 包名 | `sin` | **`openbus`**（跟随项目改名） |
| 崩溃影响 | 一个插件崩溃导致全部插件不可用 | **崩溃隔离，自动重启** |
| UI 支持 | PyQt6 独立窗口 | **保持（PyQt6 独立窗口）** |
| 主程序侵入 | v1 需在帧调度代码中插入 `PluginManager::onFrameReceived()` 调用 | **零侵入——`connect()` 到现有信号，不修改任何现有源文件** |
| 协议稳定性 | 无版本协商，方法可随意变更 | **协议契约冻结，版本化协商，仅增量演进** |

**迁移策略**：
1. SDK 包名从 `sin` 改为 `openbus`，保留 `import sin` 兼容别名
2. 现有 `plugin.json` 格式保持兼容，新增字段为可选
3. 现有 3 个示例插件（hello-world / frame-counter / ui-demo）适配新 SDK
4. C++ 侧 `PluginHost` 从单进程改为多进程，`PluginManager` 增加订阅路由
5. **移除 v1 在主程序帧调度中的硬编码调用**，改为 `PluginManager::initialize()` 中 `connect()` 到现有信号

##### 10. 目录结构

```
openbus/
├── src/core/plugin/
│   ├── plugininfo.h/cpp        # 插件元数据解析
│   ├── pluginhost.h/cpp        # 单插件进程管理 + 双通道通信
│   ├── pluginmanager.h/cpp     # 全局管理 + 订阅路由
│   └── pluginsubscription.h    # 订阅过滤表
├── sdk/openbus/                # Python SDK
│   ├── __init__.py
│   ├── _transport.py           # Named Pipe 传输
│   ├── _binary.py              # 二进制编解码
│   ├── context.py              # ExtensionContext
│   ├── frames.py               # 帧订阅 + CanFrame
│   ├── signals.py              # 信号订阅 + 编解码
│   ├── output.py / commands.py / workspace.py / statistics.py / statusbar.py
│   └── ui.py                   # PyQt6 独立窗口
├── scripts/
│   └── openbus_host.py         # 插件宿主脚本（每插件进程入口）
├── plugins/                    # 插件目录
│   ├── hello-world/
│   │   ├── plugin.json
│   │   └── main.py
│   ├── frame-counter/
│   └── ui-demo/
```

##### 11. 实施计划

| 阶段 | 内容 | 产出 |
|------|------|------|
| **Phase 0** | 协议契约冻结 | 定义并冻结二进制协议格式、JSON-RPC 方法清单、版本协商规则；编写协议文档 |
| **Phase 1** | 传输层重构 | Named Pipe 双通道、二进制协议编解码、`PluginHost` 多进程 |
| **Phase 2** | 订阅系统 | `PluginSubscription` 过滤表、订阅路由、帧批量缓冲 |
| **Phase 3** | SDK 重写 | `openbus` Python 包、`ExtensionContext` API、二进制解码 |
| **Phase 4** | 插件宿主 | `openbus_host.py` 重写、PyQt6 事件循环集成 |
| **Phase 5** | 示例迁移 | 3 个示例插件适配新 SDK、端到端测试 |
| **Phase 6** | UI 集成 | ExtensionsTab 对接新管理器、插件市场 UI |

---

## 开源组件选型清单

> 区分三大模块：Trace 报文表格组件、Graphic 实时曲线组件、协议辅助工具组件，附带授权、适用场景、优缺点，适配 CAN/CAN FD/EtherCAT 分析仪上位机。
>
> Trace / Graphic 两大核心模块的完整选型结论与使用约定已分离至设计文档：
> [doc/Trace模块设计文档.md](doc/Trace模块设计文档.md) §六、[doc/Graphic模块设计文档.md](doc/Graphic模块设计文档.md) §四。

### 一、Graphic 曲线绘图（最高优先级，核心刚需）

> 选型结论：**QCustomPlot 已采用**（深度实现多轴堆叠+卡尺系统）。完整对比与使用约定见 [doc/Graphic模块设计文档.md](doc/Graphic模块设计文档.md) §四。

#### 1. QCustomPlot（首推）

- **协议**：GPLv2 / 商业授权可购买
- **能力**：二维曲线、多 Y 轴、游标标记、区间选取、缩放平移、实时数据流、多条曲线、自定义坐标轴、导出图片
- **适配场景**：信号波形绘制，对标 CANoe Graphics
- **优势**：轻量、纯 Qt、无第三方依赖；文档丰富，工业上位机大量在用；支持大数据分片渲染
- **短板**：超大点数（百万级）直接渲染卡顿，需要自己实现数据抽稀、视口采样；不内置多通道同步游标

**工程建议**：实现视口数据降采样，配合环形缓冲区，完美适配总线录制回放场景

#### 2. Qt Charts（Qt 官方）

- **协议**：GPL / Qt 商业许可
- **能力**：折线、散点，内置坐标轴管理
- **优势**：官方原生，接口规范
- **短板**：性能弱，海量实时数据延迟高；定制化样式麻烦；商用需要购买 Qt License，不建议独立工具产品商用

#### 3. Qwt

- **协议**：Qwt License（GPL 兼容，商用需留意条款）
- **能力**：科学绘图，游标、标尺、多轴，实时数据流支持优秀
- **优势**：性能优于 QtCharts，工业老牌组件
- **短板**：UI 风格老旧；学习成本高于 QCustomPlot；新版本对 Qt6 适配存在少量兼容坑

#### 4. ImGui-Qt（备选）

- 嵌入式/工具软件高频使用，超高渲染性能；适合想要高性能曲线、自定义面板
- 代价：界面需要大量手写，无法 Qt Designer 拖拽

### 二、Trace 报文表格组件（报文列表、筛选、高亮）

> 选型结论：**Qt 虚拟 Model（QAbstractTableModel + 环形缓冲 + 视窗代理）已采用**。选型分析与避坑见 [doc/Trace模块设计文档.md](doc/Trace模块设计文档.md) §六。

Qt 原生 QTableWidget/QTreeWidget 基础上扩展，没有开箱即用的工业 Trace 控件，以下是可复用扩展库：

#### 1. QAdvancedTableView

- **功能**：虚拟表格（最重要！），支持百万行数据不卡顿、列过滤、排序、自定义行着色
- **原理**：虚拟 Model，不在内存创建全部 Item，对标 CANoe Trace 海量报文滚动
- **适用**：Trace 窗口核心表格底座

**重点**：原生 QTableWidget 加载 10 万行直接卡死，必须使用 QAbstractItemModel 虚拟表格

#### 2. QtFilterProxyModel（Qt 官方扩展）

QSortFilterProxyModel 增强版本，支持多条件、正则、多列联合过滤；实现 Trace 多维度报文筛选（ID、信号值、原始数据）。

#### 3. QtTreePropertyBrowser

- **用途**：实现 DBC 信号树、报文详情面板（Detail View）
- 可以直接展示报文内所有信号、数值、单位，构建信号浏览器侧边栏

### 三、辅助通用组件（高亮、日志、命令行、DBC 解析）

#### 1. Syntax Highlight 代码/十六进制编辑器

- **QHexEdit2** ✅【强烈推荐】
  - 十六进制查看控件，用于展示 CAN/EtherCAT 原始帧 Data，支持选中、高亮、查找；BSD 协议，宽松商用
- **QScintilla**：语法高亮，可用来实现底部命令输入窗口（类似 Wireshark 过滤表达式输入框）

#### 2. DBC 解析库（C++，无 Qt 依赖）

- **libdbc / dbccpp**：开源 DBC 文件解析，解析报文、信号、换算公式
- **CANdb++**：老牌解析库

⚠️ **注意**：大多开源 DBC 库仅基础解析，需要自行扩展信号字节序、缩放偏移计算

#### 3. BLF/ASC 日志读写

- **[vector_blf](https://github.com/Technica-Engineering/vector_blf)**（首推）
  - LGPL 协议，C++ 实现，兼容 binlog API 7.1.0
  - 支持 CAN/CAN FD/LIN 等多种对象类型，支持读写
  - 源码引入 `third_party/vector_blf/`，CMake 集成
- **自写 ASC 解析器**（~300 行）
  - ASC 为纯文本格式，每行一条报文记录，自写解析器即可
  - 同时支持导入和导出
- **自写 CSV 解析器**（~200 行）
  - 通用表格格式，支持导入和导出

### 四、EtherCAT、CAN FD 协议辅助

- **soem**（Simple Open EtherCAT Master）：开源 EtherCAT 协议栈；可用于解析 EtherCAT 报文结构
- **can-utils** 源码内协议解析逻辑，可移植用于原始帧解析

### 五、UI 布局、可停靠窗口（对标 VS Code、CANoe 多窗口拖拽）

#### Qt Advanced Docking System (QADS) ⭐关键组件

- **协议**：MIT（商用友好）
- **能力**：窗口自由拖拽、悬浮、分栏、标签化、保存布局配置文件
- 完美实现需求：可自由拖动 Trace、Graphic、信号树、统计面板，布局记忆，对标 VS Code 布局体系

**必备组件**，原生 QDockWidget 功能简陋，不要使用。

### 六、推荐最终技术组合（商用友好优先）

1. **绘图**：QCustomPlot
2. **可停靠自由布局**：Qt Advanced Docking System
3. **Trace 表格**：Qt 虚拟 QAbstractItemModel + QAdvancedTableView
4. **原始数据查看**：QHexEdit2
5. **信号树**：QtTreePropertyBrowser
6. **过滤层**：增强版 QSortFilterProxyModel
7. **DBC 解析**：dbcppp
8. **BLF 日志**：vector_blf（LGPL）
9. **ASC/CSV 日志**：自写解析器（文本格式，简单可靠）

### 七、关键避坑

1. 优先选择 MIT / BSD 协议组件，规避 GPL 传染风险（产品售卖非常重要）
2. 曲线控件一定要提前规划数据降采样，回放几十上百万帧时，直接渲染必然卡顿
3. Trace 表格强制使用虚拟 Model，绝对不要使用 QTableWidget 加载大量报文
4. QCustomPlot 默认单线程渲染；实时高流速报文，需要分离数据缓存线程与 UI 线程

---

## 硬件接入规划

接入 ZLG、PEAK、开源 USB-CAN 三大类设备，释放硬件全部能力。不存在现成库统一覆盖三家，需自建抽象层 + 分厂商封装。

### 架构集成方案

在现有 `CanSimulator` / `Player` / `Recorder` 管线中插入 `ICanDevice` 抽象层：

```
ICanDevice (src/core/candevice.h)
├── CanDeviceZLG       — 动态加载 zlgcan.dll，封装全部原生 API
├── CanDevicePeak       — 封装 PCAN-Basic 库
├── CanDeviceCandleLight — libusb + GS_USB 协议（推荐的开源设备路径）
└── CanDeviceSlcan     — 串口文本协议（兼容老旧开源硬件）
```

- 通用帧收发走虚基类 `Open/Send/Recv`
- 厂商特有功能（硬件时间戳、滤波、错误帧、ISO/Non-ISO 切换）通过 `VendorCtrl(cmd, args)` 扩展，不阉割能力
- 工厂模式枚举设备（ZLG `ZCAN_FindDevice` / PCAN `CAN_GetValue` / libusb 扫 VID:PID），动态创建实例
- DLL 缺失时优雅降级（`LoadLibrary` + 友好提示），不影响其他后端

### 厂商 SDK

| 厂商 | 库 | 能力 | 下载 |
|------|----|------|------|
| ZLG | `zlgcan.dll` / `libzlgcan.so` | CAN/FD、硬件滤波、总线负载、错误帧、硬件时间戳 | <https://manual.zlg.cn/web/#/152?page_id=5332> |
| PEAK | PCAN-Basic（免费） | CAN/FD/XL、ISO 切换、硬件时间戳、通道状态 | <https://www.peak-system.com/PCAN-Basic.239.0.html> |
| 开源（推荐） | CandleLight / GS_USB | CAN FD、硬件时间戳、USB HID 高速 | [固件](https://github.com/candle-usb/candleLight_fw) · [C++ 参考](https://github.com/GreatScottG/gs_usb_cpp) |
| 开源（兼容） | SLCAN | 串口文本协议，无硬件时间戳 | [协议规范](https://github.com/canable/slcan-spec) |

### 风险提示

1. ZLG Linux 端仅提供预编译 `libzlgcan.so`，无开源
2. SLCAN 设备普遍无硬件时间戳，界面需区分“硬件时间戳”与“软件接收时间”
3. 厂商 DLL 使用 `LoadLibrary` 动态加载，避免缺失 DLL 导致程序崩溃
4. PCAN-Basic 商用分发需保留版权声明

### UI 交互

侧边栏 ActivityBar 按钮名为「设备连接」，展开后显示设备系列树（ZLG、PEAK、Kvaser、开源 USB-CAN 等），不显示配置参数。点击设备条目后跳转到「设备连接」标签页，所有参数配置（通道、波特率、CAN FD、高级时序等）均在标签页中完成，以支持不同设备类型的参数差异。

### 实现状态

| 组件 | 状态 | 说明 |
|------|------|------|
| `ICanDevice` 抽象接口 | ✅ 已实现 | `src/core/candevice.h` |
| `CanDeviceZLG` 后端 | ✅ 已实现 | 动态加载 `zlgcan.dll`，支持 USBCAN-1/2、USBCANFD-200U/100U |
| `CanDeviceManager` 桥接层 | ✅ 已实现 | `src/core/candevicemanager.cpp`，与 `CanSimulator` 同信号接口 |
| `DevicePanel` 侧边栏 | ✅ 已实现 | 设备系列树入口，点击跳转标签页 |
| `DeviceConnectionTab` 标签页 | ✅ 已实现 | `src/ui/deviceconnectiontab.cpp`，含通道/波特率/CAN FD/高级时序参数 |
| `CanDevicePeak` 后端 | ⬜ 待实现 | 封装 PCAN-Basic 库 |
| `CanDeviceCandleLight` 后端 | ⬜ 待实现 | libusb + GS_USB 协议 |
| `CanDeviceSlcan` 后端 | ⬜ 待实现 | 串口文本协议 |

---

## 工程化管理框架设计方案

> **状态：方案评审中，待确认后实施**
>
> 参照 VS Code（Multi-root Workspace）、IAR Embedded Workbench（.eww/.ewp 分层）、CANoe（.cfg 配置体系）、Qt Creator（Session 会话管理）的工程化思路，为 openbus 设计一套完整的工程管理框架，实现不同工程互相独立、归档、快速打开历史工程、同时管理多个工程。

### 一、现状分析与问题

| 维度 | 当前实现 | 痛点 |
|------|---------|------|
| **单/多工程** | `ProjectManager` 单例管理一个工程，侧边栏列表可切换 | 切换时需保存当前→加载目标，状态恢复不完整；无法同时查看两个工程的 Trace 对比 |
| **工程文件** | `.openbusproj`（JSON，已有） | 文件格式可用，但 DBC/日志等外部资源使用绝对路径，工程文件移动后路径失效 |
| **最近工程** | AppConfig `settings.json` 中的 `project.recent` 数组 | 仅存文件路径，无元数据（修改时间、设备类型、备注）；无法搜索/筛选 |
| **归档** | 无 | 项目完成后无法一键打包归档，历史数据散落各处 |
| **会话状态** | `ProjectState` 内嵌在 .openbusproj 中 | UI 状态（窗口布局、断点、书签）与工程配置耦合，不适合多工程场景 |
| **工程隔离** | 切换时 `captureProjectState()` + `applyProjectState()` | 数据模型全局单例，切换工程时需清空/重建，无法并行运行 |

### 二、设计目标

1. **工程独立** — 每个工程拥有独立的 DBC、设备配置、Trace 数据、Graphic 视图，互不干扰
2. **工作区聚合** — 将相关工程组织到工作区中，一键恢复整组工程上下文（对标 VS Code `.code-workspace`）
3. **快速访问** — 启动页/欢迎页展示最近工程与工作区，搜索/标签/排序快速定位
4. **归档冷存储** — 完成的工程一键归档为 `.openbusarch`（ZIP），包含 .openbusproj + 关联 DBC + 日志快照
5. **并行管理** — 侧边栏工程面板展示多工程列表，支持同时打开多个工程的 Trace/Graphic 对比
6. **少重复造轮子** — 序列化用已集成的 nlohmann/json；压缩归档用 zlib（已通过 vector_blf 间接引入）；文件监控用 Qt `QFileSystemWatcher`；不做自研 IDE 框架

### 三、分层架构设计

#### 参考模型对比

| 软件 | 工作区文件 | 工程文件 | 会话文件 | 归档机制 |
|------|-----------|---------|---------|---------|
| **VS Code** | `.code-workspace`（JSON，多文件夹引用） | 文件夹（无单独工程文件） | `workspace.json`（个人 UI 状态） | 无内置归档（靠 Git） |
| **IAR EW** | `.eww`（XML，引用多个 .ewp） | `.ewp`（XML，编译配置） | `.eww` 内联（个人状态） | 无 |
| **Qt Creator** | Session（`.qts` 隐式） | `.pro` / `CMakeLists.txt` | Session 文件（个人状态） | 无 |
| **CANoe** | 无工作区概念 | `.cfg`（可读文本） | `.cfg` 内联 | 无 |
| **openbus（本方案）** | `.openbusws`（JSON，引用多个 .openbusproj） | `.openbusproj`（JSON，已有） | `sessions.json`（个人 UI 状态） | `.openbusarch`（ZIP） |

#### 文件格式定义

**1. 工程文件 `.openbusproj`（已有，需增强）**

```jsonc
{
  "version": 2,              // 格式版本号，用于向后兼容
  "meta": {
    "name": "制动系统测试",
    "created": "2026-08-07T10:00:00",
    "modified": "2026-08-07T14:30:00",
    "author": "",
    "tags": ["制动", "CAN-FD"],  // 用户自定义标签
    "notes": ""                  // 用户备注
  },
  // 资源引用 — 改为相对路径（相对于 .openbusproj 所在目录）
  "resources": {
    "dbc": ["configs/brake.dbc", "configs/steering.dbc"],
    "logs": ["data/20260807_session1.blf"],
    "filters": ["filters/brake_filter.sfilter"]  // 新增：保存的表达式过滤器
  },
  // 设备配置
  "device": {
    "type": "USBCANFD_200U",
    "channel": 1,
    "baudrate": 500000,
    "fd": true,
    "fdBaudrate": 2000000
  },
  // Trace/Graphic 实例（已有，保持不变）
  "traces": [...],
  "graphics": [...],
  // 打开的标签页（已有，保持不变）
  "tabs": {"open": [...], "active": "..."}
}
```

**核心改动**：外部资源路径从绝对路径改为相对路径，工程文件移动/拷贝后仍然有效。

**2. 工作区文件 `.openbusws`（新增）**

```jsonc
{
  "version": 1,
  "meta": {
    "name": "底盘域测试套件",
    "created": "2026-08-07T10:00:00",
    "modified": "2026-08-07T14:30:00"
  },
  "projects": [
    {"path": "brake/brake.openbusproj", "active": true},
    {"path": "steering/steering.openbusproj"},
    {"path": "suspension/suspension.openbusproj"}
  ],
  "shared": {
    "dbc": ["shared/J1939.dbc"],  // 工作区级共享 DBC
    "tags": ["底盘域"]
  }
}
```

- 工作区文件与工程文件放在同一根目录下，工程路径为相对路径
- `shared.dbc` 下的 DBC 在工作区内所有工程中自动可用（对标 VS Code workspace settings）
- 打开 `.openbusws` 等于一次性恢复整组工程上下文

**3. 会话文件 `sessions.json`（新增，个人状态）**

```jsonc
{
  "lastWorkspace": "D:/projects/chassis/chassis.openbusws",
  "recent": [
    {
      "path": "D:/projects/chassis/chassis.openbusws",
      "type": "workspace",
      "name": "底盘域测试套件",
      "modified": "2026-08-07T14:30:00",
      "pinned": true
    },
    {
      "path": "D:/projects/brake/brake.openbusproj",
      "type": "project",
      "name": "制动系统测试",
      "modified": "2026-08-07T10:00:00",
      "pinned": false
    }
  ],
  "ui": {
    "windowGeometry": "...",
    "sidebarVisible": true,
    "activePanel": "project"
  }
}
```

- 存储位置：`QStandardPaths::AppDataLocation/openbus/sessions.json`（与 `settings.json` 同目录）
- 个人状态不进入 .openbusproj / .openbusws，保持工程文件可分享（对标 Qt Creator Session 设计）

**4. 归档文件 `.openbusarch`（新增）**

- 实质上是 ZIP 压缩包，扩展名 `.openbusarch`
- 内容：
  ```
  brake.openbusarch
  ├── brake.openbusproj
  ├── configs/          # DBC 文件
  │   ├── brake.dbc
  │   └── steering.dbc
  ├── data/             # 日志文件（可选，用户勾选）
  │   └── 20260807_session1.blf
  └── manifest.json     # 归档清单（时间、源路径、文件校验）
  ```
- 实现：使用 zlib（已间接通过 vector_blf 引入）或 miniz（单文件库）进行 ZIP 打包
- 归档后可选删除源文件或标记为「已归档」

#### 架构分层

```
src/core/
├── projectmanager.h/cpp        # 已有 — 升级为多工程管理
├── workspacemanager.h/cpp      # 新增 — .openbusws 工作区文件管理
├── projectarchive.h/cpp        # 新增 — .openbusarch 归档打包/解包
├── sessionmanager.h/cpp        # 新增 — sessions.json 会话状态管理
├── resourceresolver.h/cpp      # 新增 — 相对路径→绝对路径解析器
└── ...（已有模块）
```

### 四、核心模块设计

#### 4.1 WorkspaceManager — 工作区管理器

```cpp
class WorkspaceManager : public QObject {
    Q_OBJECT
public:
    static WorkspaceManager *instance();

    // 工作区生命周期
    bool openWorkspace(const QString &filePath);
    bool saveWorkspace();
    bool saveAsWorkspace(const QString &filePath);
    void closeWorkspace();

    // 工程管理（工作区内）
    bool addProject(const QString &projFilePath);
    bool removeProject(int index);
    void setActiveProject(int index);

    // 查询
    QString workspacePath() const;
    QString workspaceName() const;
    QStringList projectPaths() const;      // 已解析为绝对路径
    int activeProjectIndex() const;
    QStringList sharedDbcFiles() const;

signals:
    void workspaceOpened(const QString &name);
    void workspaceClosed();
    void projectAdded(int index);
    void projectRemoved(int index);
    void activeProjectChanged(int index);
};
```

- 不持有 ProjectState 数据，仅管理工作区文件 I/O 和工程引用列表
- 工程数据的加载/保存仍由 `ProjectManager` 负责
- 支持无工作区模式（直接打开单个 .openbusproj，隐式创建临时工作区）

#### 4.2 ProjectManager — 升级为多工程

当前 `ProjectManager` 是单例管理单个 `ProjectState`。升级方案：

```cpp
class ProjectManager : public QObject {
    Q_OBJECT
public:
    static ProjectManager *instance();

    // 单工程操作（已有，保持兼容）
    void newProject(const QString &name);
    bool loadProject(const QString &filePath);
    bool saveProject(const QString &filePath = QString());

    // 多工程操作（新增）
    int openProjectCount() const;
    ProjectState *projectState(int index);
    ProjectState *activeProjectState();
    int activeProjectIndex() const;
    void setActiveProject(int index);
    bool closeProject(int index);
    bool isProjectModified(int index) const;

    // 资源路径解析
    QString resolvePath(const QString &relativePath, int projIndex = -1) const;

signals:
    void projectLoaded(const QString &name);
    void projectClosed(int index);
    void activeProjectChanged(int index);
    void stateModified();
};
```

- 内部 `QList<ProjectState>` 替换单个 `ProjectState`
- `m_activeIndex` 跟踪当前活跃工程
- 所有资源路径通过 `resolvePath()` 解析为绝对路径
- `captureProjectState()` / `applyProjectState()` 按 `m_activeIndex` 操作

#### 4.3 SessionManager — 会话管理器

```cpp
class SessionManager : public QObject {
    Q_OBJECT
public:
    static SessionManager *instance();

    void load();       // 启动时从 sessions.json 加载
    void save();       // 退出/切换时保存

    // 最近列表管理
    QVariantList recentItems() const;          // 含元数据
    void addRecent(const QString &path, const QString &type, const QString &name);
    void removeRecent(const QString &path);
    void pinRecent(const QString &path, bool pinned);
    void clearRecent();

    // 最后打开的工作区/工程
    QString lastOpenedPath() const;
    QString lastOpenedType() const;  // "workspace" / "project"

    // UI 状态
    void saveUiState(const QByteArray &geometry, const QByteArray &windowState);
    QByteArray uiGeometry() const;
    QByteArray uiWindowState() const;

signals:
    void recentChanged();
};
```

- 存储位置：`AppDataLocation/openbus/sessions.json`
- 与 `AppConfig` 解耦：AppConfig 管理全局应用设置，SessionManager 管理会话状态

#### 4.4 ProjectArchive — 归档器

```cpp
class ProjectArchive : public QObject {
    Q_OBJECT
public:
    // 归档：将 .openbusproj + 关联资源打包为 .openbusarch
    static bool archive(const QString &projFilePath,
                        const QString &outputPath,
                        bool includeLogs = false,
                        QWidget *parent = nullptr);  // 用于进度对话框

    // 解档：从 .openbusarch 恢复工程
    static bool extract(const QString &archPath,
                        const QString &outputDir,
                        QWidget *parent = nullptr);

    // 验证归档完整性
    static bool verify(const QString &archPath);
};
```

- 压缩实现：优先复用 zlib（vector_blf 已依赖），或引入 miniz（单文件 ~3000 行，MIT 协议）
- 归档前自动将绝对路径转为相对路径写入 .openbusproj
- 解档后自动将相对路径转回绝对路径
- 大文件日志（.blf）归档可选（`includeLogs` 参数），避免归档包过大

### 五、UI 交互设计

#### 5.1 欢迎页/启动页

参照 VS Code Welcome 页和 Qt Creator Welcome Mode：

```
┌──────────────────────────────────────────┐
│  openbus — CAN/CAN FD 报文分析工具              │
│                                          │
│  ┌─ 新建 ─────────────────────────────┐  │
│  │  [新建工程]  [新建工作区]           │  │
│  └────────────────────────────────────┘  │
│                                          │
│  ┌─ 最近 ─────────────────────────────┐  │
│  │  📌 底盘域测试套件      工作区  8/7 │  │
│  │     └ 制动系统测试      工程   8/7 │  │
│  │     └ 转向系统测试      工程   8/6 │  │
│  │  📌 ECU诊断验证         工程   8/5 │  │
│  │     导航信号分析        工程   8/3 │  │
│  └────────────────────────────────────┘  │
│                                          │
│  [打开工程文件...]  [打开工作区文件...]   │
└──────────────────────────────────────────┘
```

- 启动时如果 `sessions.json` 有 `lastOpenedPath`，可直接恢复上次状态（可配置）
- 📌 图标表示已固定（pinned）
- 搜索框过滤最近列表

#### 5.2 侧边栏工程面板（升级）

```
┌─ 工程面板 ─────────────────────┐
│  📁 底盘域测试套件 (工作区)     │
│  ├── ✅ 制动系统测试  [活跃]    │
│  ├── ⬜ 转向系统测试            │
│  └── ⬜ 悬架系统测试            │
│  ──────────────────────────    │
│  最近工程                      │
│  • ECU诊断验证                 │
│  • 导航信号分析                │
│  ──────────────────────────    │
│  [新建] [打开] [归档] [搜索]    │
└────────────────────────────────┘
```

- 工作区模式下展示工程树，点击切换活跃工程（不卸载其他工程数据）
- 双击工程名展开/折叠工程详情（DBC 数量、Trace 数量等）
- 右键菜单：设为活跃 / 在新标签页打开 / 归档 / 从工作区移除
- 搜索框实时过滤工程列表

#### 5.3 归档对话框

```
┌─ 归档工程 ──────────────────────┐
│  源工程: D:/projects/brake/brake.openbusproj  │
│  归档到: D:/archive/brake_20260807.openbusarch │
│  ☑ 包含 DBC 文件 (2 个, 1.2 MB)            │
│  ☐ 包含日志文件 (1 个, 45 MB)             │
│  ☑ 归档后标记源工程为「已归档」             │
│  ☐ 归档后删除源文件                       │
│                          [取消] [归档]    │
└────────────────────────────────────────────┘
```

### 六、数据模型变更

#### 6.1 路径策略：绝对→相对

当前 .openbusproj 中的 DBC 路径、日志路径为绝对路径（如 `D:/projects/brake/configs/brake.dbc`）。
新方案改为相对于 .openbusproj 所在目录的路径（如 `configs/brake.dbc`）。

```cpp
// ResourceResolver — 路径解析器
class ResourceResolver {
public:
    explicit ResourceResolver(const QString &projFilePath);

    // 相对路径 → 绝对路径（加载时调用）
    QString resolve(const QString &relativePath) const;

    // 绝对路径 → 相对路径（保存时调用）
    QString relativize(const QString &absolutePath) const;

private:
    QString m_projDir;  // .openbusproj 所在目录
};
```

- 使用 `QDir::relativeFilePath()` 和 `QDir::absoluteFilePath()` 实现
- 外部驱动器/不同盘符的路径保持绝对路径（Windows 跨盘符无法相对化时的回退策略）

#### 6.2 多工程数据隔离

当前数据模型（`CanTraceModel`、`DbcManager` 等）为全局单例，多工程场景需要隔离：

| 组件 | 当前 | 方案 |
|------|------|------|
| `CanTraceModel` | 全局单例 | 工程级实例（每个工程一个），切换活跃工程时 swap 指针 |
| `DbcManager` | 全局单例 | 工程级实例，工作区级共享 DBC 注入到各工程实例 |
| `CanDeviceManager` | 全局单例 | 保持单例（同时只能连接一个物理设备），但设备配置按工程存储 |
| `GraphicView` | 标签页级 | 已天然隔离（每个标签页一个 GraphicView 实例） |

**过渡策略**：第一阶段不改为多实例，而是在切换工程时完整 `capture → save → load → apply`，实现"伪并行"（同时加载在内存，但同一时间只显示一个）。第二阶段再实现真正的多工程 Tab 并行显示。

### 七、开源组件选型

| 需求 | 推荐方案 | 协议 | 集成方式 | 理由 |
|------|---------|------|---------|------|
| **JSON 序列化** | nlohmann/json | MIT | ✅ 已集成 | 无需额外引入，直接用于所有文件格式 |
| **ZIP 压缩** | miniz | MIT | 源码引入 `third_party/miniz/` | 单文件库（~1 个 .c + .h），API 简洁，zlib 兼容；比直接用 zlib 更简单 |
| **ZIP 压缩（备选）** | zlib | zlib | ✅ 已通过 vector_blf 间接引入 | 无需额外引入，但 API 较低层，需要手动处理 ZIP 容器格式 |
| **文件监控** | QFileSystemWatcher | Qt 内置 | 无需引入 | 监控工程目录外部变更（如 DBC 文件被外部修改） |
| **路径解析** | QDir | Qt 内置 | 无需引入 | `relativeFilePath()` / `absoluteFilePath()` 满足需求 |
| **时间戳** | QDateTime | Qt 内置 | 无需引入 | ISO 8601 格式，`Qt::ISODateWithMs` |
| **元数据搜索** | 自写 | — | ~100 行 | 对 recent 列表的 name/tags 做 `contains()` 过滤，无需全文搜索引擎 |

**不引入的组件及理由**：
- **SQLite** — 工程数量在百级以内，JSON 文件 + 内存遍历完全够用，引入数据库增加部署复杂度
- **自研 IDE 框架** — openbus 不是 IDE，不需要代码索引/构建系统/调试器等通用 IDE 能力
- **Libarchive** — 归档需求仅限 ZIP 格式，miniz 足够，libarchive 引入 10+ 文件过重

### 八、实施计划

#### Phase 1 — 基础设施（1-2 天）

1. **ResourceResolver** — 相对路径解析器
2. **ProjectState v2** — 增加 `meta`（标签/备注/时间戳）、`resources`（相对路径）字段
3. **向后兼容** — 加载 v1 格式时自动转换绝对路径为相对路径，保存为 v2
4. **SessionManager** — 从 AppConfig 迁移 `project.recent` 到独立 `sessions.json`，增加元数据

#### Phase 2 — 工作区（2-3 天）

1. **WorkspaceManager** — .openbusws 文件读写
2. **ProjectManager 多工程** — `QList<ProjectState>` 替换单实例，`m_activeIndex` 跟踪
3. **ProjectPanel UI 升级** — 工作区工程树 + 切换 + 右键菜单
4. **欢迎页** — 最近列表 + 新建/打开入口

#### Phase 3 — 归档（1-2 天）

1. 引入 miniz 到 `third_party/`
2. **ProjectArchive** — 打包/解包/验证
3. **归档对话框 UI**
4. CMake 集成 miniz 编译

#### Phase 4 — 多工程并行（3-5 天，可选）

1. `CanTraceModel` / `DbcManager` 改为工程级实例
2. 切换活跃工程时 swap 指针（O(1) 切换，无数据重建）
3. 多工程 Trace 对比标签页
4. 工作区级共享 DBC 注入

### 九、风险与注意事项
现在的软件版本还没有发布过，不存在兼容性问题，可以不考虑兼容问题。
1. **路径迁移** — 从绝对路径迁移到相对路径时，如果 DBC 文件不在 .openbusproj 同目录下（如 `D:/Qt/...`），保持绝对路径，不做错误的相对化
2. **文件锁** — Windows 上 .openbusproj 被外部编辑器打开时保存可能失败，需 try-catch + 友好提示
3. **归档大小** — BLF 日志可能数百 MB，归档时默认不包含日志，用户显式勾选
4. **miniz vs zlib** — miniz 更简单但功能较少（无 ZIP64 支持，单文件 < 4GB 足够）；zlib 已引入但需手动实现 ZIP 容器逻辑
5. **多工程内存** — 每个工程的 Trace 数据可能很大（万帧级），Phase 4 前需评估内存上限，必要时仅活跃工程加载数据，非活跃工程仅加载配置
6. **文件格式版本号** — 所有文件格式（.openbusproj / .openbusws / sessions.json）都带 `version` 字段，为未来格式升级预留

---

## Trace 高性能优化方案

> **完整方案已分离至 [doc/Trace模块设计文档.md](doc/Trace模块设计文档.md) §三** — 瓶颈分析、4 阶段优化方案、性能目标、验证方法。
>
> 实施进度（参考 Wireshark 延迟格式化 / TSMaster 可调刷新率 / CANoe 环形缓冲）：
>
> | Phase | 内容 | 状态 |
> |-------|------|------|
> | Phase 1 | 延迟格式化 + 可见行缓存（RowCache，滚动性能 ×10-50） | ✅ 已实现 |
> | Phase 2 | 批量更新 + 可调刷新率（高/中/低/暂停，CPU 降低 90%+） | ✅ 已实现 |
> | Phase 3 | 增量过滤代理（CanTraceProxyModel 替代 QSortFilterProxyModel） | ✅ 已实现（2026-08-17，新增行 O(1) 过滤） |
> | Phase 4 | 环形缓冲区存储（百万帧，seqCounter 键） | ✅ 已实现 |
>
> 性能目标：10 万帧加载 <1s、滚动 60fps、5000帧/s 实时捕获 CPU<20%、百万帧加载 <5s 内存 <500MB。

---


# 插件扩展机制

参考 VSCode 的 Extension Host 架构，采用 **子进程隔离 + JSON-RPC** 方案。插件用 Python 编写，崩溃不影响主程序。

## 一、架构

```
┌─ sin 主程序 (C++/Qt) ──────────────┐
│  PluginManager ── PluginHost(QProcess) │
│       ↕ JSON-RPC over stdin/stdout       │
└─────────────────────────────────────┘
          ↕
┌─ Python 插件宿主 (sin_host.py) ───┐
│  sin SDK 模块  │  插件 A  │  插件 B  │
└───────────────────────────────────┘
```

**核心设计**：
- 插件运行在独立 Python 子进程中，崩溃不影响主程序
- 主程序与插件通过 newline-delimited JSON-RPC 2.0 通信
- 用户只需写 `plugin.json` + `main.py`，导入 `sin` 模块即可
- 按 activationEvents 激活（onStartup / onFrame / onCommand:id / onFileOpen:ext）
- 崩溃后指数退避自动重启（1s→2s→4s，超过 3 次放弃）

## 二、目录结构

```
sin/
├─ src/core/plugin/          # C++ 插件核心
│  ├─ plugininfo.h/cpp        #   插件清单解析
│  ├─ pluginhost.h/cpp        #   QProcess + JSON-RPC
│  └─ pluginmanager.h/cpp     #   发现/激活/消息分发
├─ scripts/sin_host.py       # Python 插件宿主
├─ sdk/sin/                  # Python SDK
│  ├─ _transport.py           #   通信层（共享 stdout 锁）
│  ├─ output.py               #   输出面板 API
│  ├─ frames.py               #   CAN 帧操作 API
│  ├─ commands.py             #   命令 API
│  ├─ workspace.py            #   工程上下文 API
│  └─ signals.py              #   DBC 信号解码 API
└─ plugins/                  # 用户插件目录
   ├─ hello-world/            #   最简示例
   └─ frame-counter/          #   帧统计示例
```

## 三、插件清单 (plugin.json)

```json
{
    "name": "j1939-decoder",
    "version": "1.0.0",
    "description": "J1939 协议解码插件",
    "main": "main.py",
    "activationEvents": ["onStartup", "onCommand:j1939.decode"],
    "contributes": {
        "commands": [{ "id": "j1939.decode", "title": "J1939: 解码" }]
    }
}
```

## 四、Python SDK API

```python
import sin

def activate(context):
    sin.output.append("插件已加载")          # 输出到面板
    context.register_command("my.cmd", handler) # 注册命令
    context.on_frame(on_frame)                 # 订阅帧
    frames = sin.frames.get_selected()         # 获取选中帧
    sin.frames.send(0x123, b'\x01\x02')        # 发送帧
    val = sin.signals.decode(0x123, data)       # 解码信号

def on_frame(frame):
    sin.output.append(f"ID=0x{frame.id:X} data={frame.data.hex()}")
```

## 五、通信协议

JSON-RPC 2.0，每行一条 JSON，`\n` 分隔。帧数据用 hex 字符串。

主程序→插件：`activate` / `deactivate` / `frameReceived` / `executeCommand` / `fileOpened`
插件→主程序：`output.append` / `sendFrame` / `registerCommand` / `frames.getSelected` / `log`

帧批量发送：每 100ms 或 100 帧批量一次，避免高频帧淹没 Python。

## 六、集成点

| 现有组件 | 集成方式 |
|----------|----------|
| MainWindow::onFrameReceived() | 末尾调用 PluginManager::onFrameReceived() |
| 菜单栏 | 新增「插件」菜单，动态填充已注册命令 |
| BottomPanel | 新增「插件」标签页显示输出 |
| CanDeviceManager::sendFrame() | 转发插件的 sendFrame 请求 |
| CanTraceModel | 响应 frames.getSelected 请求 |
| closeEvent() | 调用 PluginManager::shutdown() |


# 插件 UI 能力（方案 A — PyQt6 独立窗口）

已实现。插件在 Python 子进程中用 PyQt6 创建独立窗口，崩溃不影响主程序。

## 方案对比（选型过程）

| 方案 | 复杂度 | 插件开发难度 | 嵌入主窗口 | 独立窗口 | 稳定性 |
|------|--------|-------------|-----------|---------|--------|
| **A. PyQt6 子进程独立窗口** | **低** | **低** | ✗ | **✓** | **✓ 进程隔离** |
| B. PyQt6 + 原生窗口嵌入 | 中 | 低 | ✓ | ✓ | ✓ 但有焦点/resize 问题 |
| C. QML 动态加载 | 高 | 中 | ✓ | ✓ | ✗ 需同进程 |
| D. HTML/QWebEngineView | 高 | 低 | ✓ | ✓ | ✓ 但依赖重 |
| E. JSON 声明式 UI | 高 | 低 | ✓ | ✗ | ✓ 最安全 |

**选择方案 A 的原因**：实现简单、全平台支持（含 Wayland）、无焦点/resize/输入法问题、崩溃行为干净（窗口直接消失，无黑矩形）。方案 B 的 `createWindowContainer` 嵌入跨进程窗口在 Qt 官方文档中标注“可能无法在所有平台完美工作”，风险较高。

## 实现原理

PyQt6/PySide6 是 Qt6 的 Python 绑定，与主程序的 C++ Qt6 共享同一底层 Qt 库，渲染风格一致。
插件在 Python 子进程中用 PyQt6 创建 QMainWindow，窗口独立浮动在桌面上。

```
主程序 (C++/Qt6)                    Python 子进程 (PyQt6)
┌─────────────────────┐            ┌─────────────────────┐
│  无需任何 UI 改动     │            │  QApplication         │
│  仅 JSON-RPC 通信     │ ←───────→  │  QMainWindow (独立)   │
│                     │  JSON-RPC  │  (PyQt6 渲染)         │
└─────────────────────┘            └─────────────────────┘
```

### 关键实现

1. **sin_host.py 双模式启动**：检测 PyQt6 是否可用，可用则启动 QApplication 事件循环，否则退回简单循环
2. **后台线程读 stdin**：独立线程持续读取 stdin，响应消息直接分发（`deliver_response`），请求/通知放入队列
3. **QTimer 轮询**：主线程 QTimer 每 10ms 从队列取消息处理（Qt 要求 widget 操作在主线程）
4. **SDK ui 模块**：`sin.ui.create_window(title)` 返回 QMainWindow 供插件自定义

### sin_host.py 事件循环

```
stdin → 后台线程 → ┬─ 响应 → deliver_response()（直接分发，修复原有死锁）
                    └─ 请求/通知 → Queue → QTimer(10ms) → handle_message()（主线程）

QApplication.exec() 驱动 PyQt6 窗口事件
```

### Python SDK API

```python
import sin
from PyQt6.QtWidgets import QLabel, QVBoxLayout, QPushButton

def activate(context):
    win = sin.ui.create_window("我的工具")      # 创建独立窗口
    win.resize(400, 300)
    layout = QVBoxLayout(win.centralWidget())
    layout.addWidget(QLabel("Hello!"))
    btn = QPushButton("发送帧")
    btn.clicked.connect(lambda: sin.frames.send(0x123, b'\x01\x02'))
    layout.addWidget(btn)
    win.show()                                    # 显示窗口

    sin.ui.show_message("提示", "窗口已创建")     # 消息对话框
```

### 优势

- **PyQt6 与 C++ Qt6 共享底层库**：渲染一致，主题一致，无视觉割裂
- **进程隔离**：PyQt6 崩溃只影响插件进程，主程序不受影响
- **开发简单**：插件开发者用标准 PyQt6 API，`pip install PyQt6` 即可
- **全平台支持**：Windows / macOS / Linux (X11 + Wayland) 均可
- **无嵌入风险**：无焦点/resize/输入法问题，无黑矩形崩溃

### 注意事项

- **PyQt6 安装**：插件开发者需 `pip install PyQt6`（或 PySide6）
- **版本匹配**：PyQt6 的 Qt 版本应与主程序的 Qt6 版本一致（6.x 系列）
- **send_request 阻塞**：SDK 的 `send_request()` 会短暂阻塞 Qt 事件循环（主程序通常毫秒级响应，实际无感知）
- **窗口独立**：插件窗口浮动在桌面上，用户用 Alt+Tab 切换，不嵌入主窗口

