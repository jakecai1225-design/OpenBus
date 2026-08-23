# Watcher 观测模块方案

> 状态：**已实施**（方案 + 评审 + 落地，2026-08-23）
> 关联：doc/flow.md（Flow 画布 v1.6 统一块交互）、doc/Graphic模块设计文档.md（DataWindow 增补三）

## 一、需求背景与定位

Flow 画布原「Data 统计」块更名并重塑为 **Watcher 观测块**，定位对标
Lauterbach Trace32 / IAR / Keil 调试器的 **Watch 窗口**——分析/调试过程中
持续观测关键量的仪表盘：

1. **变量观测（Watch）**：像调试器 watch 列表一样，把关心的 DBC 信号
   （= 总线上的"变量"）加入列表，实时显示当前值/原始值/最小/最大/单位，
   支持搜索 + 树形 + Ctrl/Shift 多选批量添加；
2. **统计观测（Statistics）**：面向总线的 statistic 观测——总线负载、
   总帧数、报文 ID 数、统计时长，**错误帧分类统计**（Stuff/Form/ACK/
   Bit0/Bit1/CRC），以及逐报文（per-ID）频率/周期/抖动统计。

调试器类比：变量 watch ≈ 信号值列表；System Registers ≈ 总线负载/错误
计数；Peripherals ≈ 逐报文周期统计。

## 二、现状盘点（实施前实测代码路径）

| 组件 | 位置 | 现状 | 问题 |
|------|------|------|------|
| 画布 Data 统计块 | measurementsetupview.cpp buildTopology（id="data"） | moduleName 为空，点击 → `moduleOpened("data","")` → 壳侧输出「Data 统计模块（待实现）」 | **纯占位，无任何功能** |
| DataWindow | src/ui/datawindow.cpp（openbus_graphic 模块内缓存单例） | 信号值表格（Current/Raw/Physical/Min/Max），手动输 ID+信号名添加 | 归属 Graphic 模块；只能按 ID 手输添加；无统计 |
| BusStatistics 引擎 | src/core/busstatistics.{h,cpp}（壳持有 m_busStats） | 按 ID 统计帧数/周期/抖动/频率，错误帧分类，总线负载；1s 定时 computeStats + `statisticsUpdated` 信号 | **信号无任何消费者——统计引擎空转，无 UI**；`clear()` 无人调用（跨会话累计）；bitrate 恒为默认 500k |
| I/O Graph | m_ioGraph（壳侧成员） | 壳自持页面，onFrameReceived 直接喂帧 | 壳侧页面的既有先例 |

## 三、候选方案对比（评审）

| | 方案 A：壳侧 WatcherView（新页面，壳持有） | 方案 B：扩展 DataWindow（Graphic 模块内改名） | 方案 C：新建 openbus_watcher.dll 模块 |
|---|---|---|---|
| 统计数据来源 | 壳自持 m_busStats，**同对象直连**（快照轮询） | 引擎在壳、页面在 DLL——QVector/Summary/数组需经 QVariant 跨模块编排，`statisticsUpdated` 原生签名不可序列化 | 同 B，需经 ShellContext 编排或模块自建第二引擎（双份统计） |
| DBC 访问 | 壳自持 m_dbcManager 直连 | 模块内已有 | 需经 ctx 传指针 |
| 帧喂入 | onFrameReceived 直调（同 m_ioGraph 先例） | graphicInvoke("onFrame") 顺带 | 需新 invoke 约定 |
| 工程量 | 中（1 个新视图类 + 壳接线） | 小，但跨模块统计编排别扭、概念错位（Watcher≠Graphic） | 大（新 DLL 目标 + 注册 + 上下文），F1 多协议重构前过度投资 |
| 回归风险 | 零触碰 Trace/Graphic/DataWindow 既有链路 | 改 Graphic 模块热路径 | 全新链路 + 构建面扩大 |

**评审结论：选 A（壳侧 WatcherView）。** 理由：BusStatistics 与 DbcManager
均为壳侧持有，方案 A 是唯一"同对象直连、零跨模块编排"的路线；m_ioGraph
已验证壳侧页面先例；方案 C 的模块化收益留给 flow.md F1+ 多协议重构时
统一收割（届时 Watcher 随模块化编排再安置，本方案的壳侧页面是低成本
过渡态，接口上不留私有耦合——全部数据经公开 API 快照读取）。

规避项：统计展示**不连 `statisticsUpdated` 信号**（DEF-08：openbus_data
信号在 openbus_ui 侧 PMF connect 静默断连；且原生签名带 C 数组）——改为
**快照轮询**（500ms 定时器 + 只增 getter），顺带给引擎补上快照能力。

## 四、详细设计（按方案 A 展开）

### 4.1 画布块改造（measurementsetupview.cpp）

- `buildTopology`：块 `id "data" → "watcher"`，标题 `Data 统计 → Watcher 观测`
  （绿色主题色不变）；连线 `database → watcher`；空区右键标准模块列表同步。
- 块右键原「配置统计参数...」死占位 → 「观测变量与统计设置...」
  （`moduleOpened("watcher","")` 打开观测页）。
- 交互遵循 v1.6 统一规则：未启用单击 = 启用；已启用单击/双击 = 进入配置
  （打开 Watcher 页）；右键 = 配置/启停/增删。

### 4.2 WatcherView 布局（src/ui/watcherview.{h,cpp}，壳侧）

```
┌ 工具栏 ──────────────────────────────────────────────────────┐
│ ＋添加变量  －移除选中  ✕清空变量 │ ⏸暂停刷新 │ ⟲清零统计      │
├ QTabWidget ──────────────────────────────────────────────────┤
│ [变量观测 Watch]                                              │
│  变量 | 当前值 | 原始值 | 最小 | 最大 | 单位 | 报文            │
│  EngineRPM | 2350.000 | 0x92E | ... | RPM | 0x0C1 EngineData │
├──────────────────────────────────────────────────────────────┤
│ [总线统计 Statistics]                                         │
│  摘要卡片: 总帧数 | 报文ID数 | 总线负载 | 统计时长 | 错误帧总数  │
│  报文统计表: ID | 帧数 | 频率Hz | 平均周期ms | 最小 | 最大 |    │
│             抖动σ | 字节 | 周期性                              │
│  错误统计表: Stuff | Form | ACK | Bit0 | Bit1 | CRC | 总计    │
└──────────────────────────────────────────────────────────────┘
```

- 工具栏：添加变量（弹 DbcSignalPickerDialog，搜索/树形/多选）、移除选中、
  清空变量、暂停刷新（checkable，冻结显示——对应调试器 watch 的 freeze）、
  清零统计（`m_stats->clear()` + 变量 min/max 复位）。
- 变量表 7 列；统计页摘要 5 卡片 + per-ID 表 9 列 + 错误表 1 行 7 列。

### 4.3 数据流与刷新

```
onFrameReceived(frame)                      [壳既有枢纽]
  ├─ m_busStats->onFrame(frame)             [既有，不动]
  └─ m_watcherView->onFrame(frame)          [新增：m_latestFrames 缓存]
                                            （m_watcherView 为空则跳过）

WatcherView 内部 500ms 定时器（暂停时不刷新）：
  ├─ refreshWatchTable(): 逐条目 m_latestFrames[canId] → sig.decode()
  │   → 物理值/原始值/最小/最大（懒解码，同 DataWindow 策略；
  │     区别：条目自带 DbcSignal 拷贝，不经 DbcManager 按名回查）
  └─ refreshStatistics(): m_busStats->lastIdStats()/lastSummary()/
      lastErrorCounts() 快照 → 重建统计表（引擎 1s 计算节奏，轮询 500ms）
```

### 4.4 BusStatistics 扩展（只增不改，src/core/busstatistics.{h,cpp}）

- 新增成员 `m_lastIdStats / m_lastSummary`：`computeStats()` 计算后先存快照
  再发信号（信号路径行为零变化）；
- 新增只读 getter：`lastIdStats() / lastSummary() / lastErrorCounts(quint64*)`。
- 引擎行为修正（配套）：`onMeasurementToggled(true)` 时壳侧调用
  `m_busStats->clear()`——新测量会话统计归零（此前跨会话累计属于缺陷；
  对齐 CANoe 逐会话统计语义）。

### 4.5 变量添加（复用 DbcSignalPickerDialog）

- `DbcSignalPickerDialog` 构造函数追加可选 `title` 参数（缺省保持
  「添加信号到 Graphic」，Graphic 侧栏入口零改动）；
- Watcher 传「添加观测变量」；选中项即 watch 条目（含 canId/extended/
  报文名/DbcSignal 完整拷贝）——同对话框服务两个入口（Graphic 批量绘图、
  Watcher 批量观测）。

### 4.6 生命周期与接线（壳侧）

- `MainWindow::m_watcherView` 成员 + destroyed 置空（标签页关闭即
  deleteLater → 下次重建，同 ShortcutsPage/m_dataWindow 模式）；
- `onModuleOpened("watcher")` → `onOpenWatcher()`：复用已有则聚焦
  （openTab 按 label 去重），否则 `new WatcherView(m_dbcManager, m_busStats, this)`；
- `onMeasurementToggled(true)`（两数据源公共起点）：`m_busStats->clear()` +
  `m_watcherView->clearData()`（存在时）。
- 工具菜单追加「Watcher 观测」（Ctrl+Shift+W），与 Data Window 并列。

## 五、实施清单

| # | 文件 | 改动 |
|---|------|------|
| 1 | doc/Watcher方案.md | 本文档（含实施记录回填） |
| 2 | src/core/busstatistics.{h,cpp} | 快照成员 + getter + computeStats 存快照（只增） |
| 3 | src/ui/dbcsignalpickerdialog.{h,cpp} | 构造函数可选 title 参数（只增，默认行为不变） |
| 4 | src/ui/watcherview.{h,cpp} | **新增**：观测视图（变量表 + 统计页 + 工具栏 + 500ms 刷新） |
| 5 | src/CMakeLists.txt | SRC_UI 增加 watcherview |
| 6 | src/ui/measurementsetupview.{h,cpp} | 画布块 data→watcher（buildTopology/连线/右键/空区菜单） |
| 7 | src/ui/mainwindow.h | m_watcherView 成员 + onOpenWatcher 声明 |
| 8 | src/ui/mainwindow_pages.cpp | onOpenWatcher 实现（ShortcutsPage 生命周期模式）+ 工具菜单项 |
| 9 | src/ui/mainwindow_frameflow.cpp | onModuleOpened 分支 watcher 化；onFrameReceived 喂帧；onMeasurementToggled 复位 |
| 10 | doc/flow.md | §2.1/§8.1 拓扑文案 data→watcher + v1.6 补记 |

不触碰：DataWindow、GraphicModule、TraceModule、录制链路。

## 六、验收标准

1. 画布第 4 列块显示「Watcher 观测」；单击（已启用）或双击打开 Watcher 页；
   右键「观测变量与统计设置...」同样打开；空区右键可重新添加该块。
2. Watcher 页添加变量：搜索过滤命中、树形展开手选、Ctrl/Shift 多选批量加，
   表格实时显示值/原始值/最小/最大/单位/报文。
3. 统计页：测量运行中总帧数/ID 数/负载/时长实时刷新；per-ID 频率周期抖动
   正确；错误帧分类计数显示（模拟器无错误帧时全 0 为正确基线）。
4. 暂停冻结显示；清零统计后全部归零重新累计；关闭标签页再打开不崩溃
   （destroyed 置空链）；重新开始测量统计自动归零。
5. 全量 ctest 基线不降（8/9，唯一失败为既有 canfileio BLF roundtrip）。

## 七、实施记录（2026-08-23 回填）

- 方案 A 落地：`watcherview.{h,cpp}`（约 480 行）新增，`BusStatistics`
  快照 getter 三只增方法，`DbcSignalPickerDialog` 可选标题参数；
- 画布块/右键/空区菜单/连线全部切换为 watcher；壳侧接线四处
  （成员/开页/喂帧/复位）+ 工具菜单「Watcher 观测」快捷键 Ctrl+Shift+W；
- 测量开始新增 `m_busStats->clear()`（修正跨会话累计缺陷，方案 §4.4）；
- 构建：增量编译通过；ctest 8/9 基线持平（详见当日构建记录）。
- 文档同步：flow.md 头部 v1.6 补记（2026-08-23）+ §2.1/§8.1 拓扑文案。

## 八、已知边界与后续项

1. **bitrate 未接线**：总线负载按默认 500kbps 估算（引擎 setBitrate 无调用
   方）；设备连接后真实波特率回填留待后续（接线点：设备启动链）。
2. **Watch 条目不持久化**：页面关闭（deleteLater）即丢失观测列表；工程
   保存/恢复 Watcher 配置留待后续（可挂 layoutConfig/工程 stateJson）。
3. **CAN FD 负载估算**：引擎按经典 CAN 帧位数近似（47+DLC×8），FD 帧
   占用偏小——沿引擎既有近似，不在本次范围。
4. **watcher 块使能不门控数据**：与 record 块现状一致（原 data 块同样如此；
   仅 trace/graphic 实例块参与 onModuleToggled 门控）；块级门控语义随 F1
   FlowSession 落地时统一。
5. **多实例**：当前单实例（openTab 按 label 去重）；多 Watcher 页需求
   出现后再按 trace/graphic 实例模式扩展。
