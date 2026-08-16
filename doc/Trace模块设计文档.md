# Trace 模块设计文档

> Trace 是总线分析工具的核心视图，对标 CANoe Trace Window + Wireshark Packet List。
> 目标：万帧以上实时捕获流畅、百万帧离线加载可用，功能对标 CANoe/Wireshark 核心工作流。

---

## 一、定位与对标

### 1.1 竞品对标分析

| 对标能力 | CANoe Trace Window | Wireshark Packet List | openbus 状态 |
|---------|--------------------|-----------------------|-------------|
| 帧编号列 | ✅ | ✅ No. 首列 | ✅ 已实现 |
| 时间显示模式（绝对/相对/Delta/日期） | ✅ | ✅ 5 种模式 | ✅ 已实现（Absolute/SinceCapture/SinceDisplay/DateTimeOfDay/SecondsSinceEpoch） |
| 表达式过滤 | ✅ 预定义过滤器 | ✅ Display Filter（类 C 语法） | ✅ 已实现（自研 FilterEngine 递归下降解析器），⬜ 缺预定义过滤集管理 |
| 按列筛选 | ✅ | ✅ | ✅ 已实现（漏斗图标 + 值勾选面板：搜索/全选/反选/计数，Excel 风格） |
| 排序 | ✅ | ✅ | ✅ 已实现（3-state + 数值/时间智能比较） |
| 行着色规则 | ✅ 动态着色编辑器 | ✅ Coloring Rules | ✅ 规则引擎已实现，⬜ 缺可视化规则编辑器 UI |
| 书签标记 | ✅ | ✅ | ✅ 运行时标记 + 文件级导出导入已实现，⬜ 缺工程文件自动集成 |
| 快速搜索/跳转 | ✅ | ✅ Go to Packet / Find Next | ✅ 已实现（goToPacket/findNext/goToNextSameId） |
| 覆盖模式（同 ID 只留最新） | ✅ | — | ✅ 已实现（默认关闭，入口在设置菜单） |
| 视窗缩略导航 | — | — | ✅ 已实现（ViewportOverview，超出竞品的独创能力） |
| 海量数据性能 | ✅ 环形缓冲 + 后台解码 | ✅ 延迟格式化 + 仅可见行着色 | ✅ Phase 1/2/3/4 全部完成（2026-08-17 增量过滤代理上线） |
| 文件导入/导出 | ✅ BLF/ASC | ✅ pcap/mergecap | ✅ BLF/ASC/CSV 导入导出已实现 |
| DBC 报文名称列 | ✅ Name 列 | ✅ Protocol/Info 列 | ✅ 已实现（2026-08-17，ColName 列 + 过滤/排序支持） |
| 选中帧统计视图 | ✅ Statistics View | ⬜ 仅全局统计 | ❌ 缺（§九 G-F1） |
| 选中帧差异对比 | ✅ Difference View | ❌ | ❌ 缺（§九 G-F2） |
| 行内信号展开 | ✅ 行首 + 展开信号 | ✅ Packet Detail 树 | ⬜ 以底部信号面板替代（§九 G-F4） |
| 时间显示精度配置 | ✅ | ✅ | ✅ 已实现（-1 自动/0/3/6/9 位，设置菜单入口） |
| 字体大小调节 | ✅ 工具栏 A+/A- | ✅ Ctrl+加号 | ✅ 已实现（Ctrl+滚轮/快捷键/右键菜单 + 持久化，2026-08-17） |
| 混合事件流（错误/变量/诊断） | ✅ 同一流混显 | ✅ 多协议 | ⬜ 战略项（§九 G-F9） |

> **2026-08 按 CANoe Trace 工具栏逐项复查**：补记已实现能力（值勾选列筛选、时间精度、书签文件导出导入、标记跳转菜单），新增 9 项功能差距与 5 项 UI 风格差距，详见 **§九**。

### 1.2 核心差异化

- **Wireshark 风格导航体验**：帧编号 + Delta 时间 + 表达式过滤 + Go to Packet
- **视窗缩略图**（ViewportOverview）：左侧密度分布缩略图 + 可拖拽视窗高亮区，竞品均无
- **CAN/CAN FD 域特化**：覆盖模式、ID 帧计数列、内联信号值列、错误帧高亮

---

## 二、当前架构

### 2.1 模型-视图分层

```
CanTraceModel              (QAbstractTableModel)
  │  环形缓冲区存储 Phase 4 ✅
  │  延迟格式化 + 可见行缓存 Phase 1 ✅
  │  批量更新 + 可调刷新率 Phase 2 ✅
  ▼
CanTraceProxyModel         (QAbstractProxyModel)   ← Phase 3 增量过滤 ✅（2026-08-17）
  │  双向映射：新增行 O(1)/行，过滤变化单次 O(n) 重建
  │  主表达式过滤 (FilterEngine) + 按列/值集子过滤
  │  时间戳显示模式切换（SinceDisplay O(1) 推导）
  │  独立排序索引（流式捕获下归并插入）
  ▼
ViewportProxyModel         (QAbstractProxyModel)    ← 虚拟表格视窗层 ✅
  │  仅向视图暴露视窗范围 [start, start+size)
  │  百万帧数据仅渲染视窗内几百行
  ▼
TraceView                  (QTableView)
  │  3-state 排序 / 列布局持久化 / 选中保持(pinSelection)
  │  多选标记(Ctrl/Shift) / 右键菜单 / 双击信号
  ▼
TraceTab                  (页面容器)
     FilterBar + 工具条 + TraceView + ViewportOverview
     + Trace Explorer 底部标签 (详情/信号/统计/差异) ✅ T8-T10
```

### 2.2 关键类职责

| 类 | 文件 | 职责 |
|----|------|------|
| `CanTraceModel` | `src/models/cantracemodel.h/cpp` | 数据模型：环形缓冲、行缓存、批量刷新、着色规则、标记/标签、覆盖模式、时间参考点 |
| `CanFilterProxyModel` | `src/models/canfilterproxymodel.h/cpp` | 过滤代理：主表达式 + 列过滤 + 5 种时间戳模式 |
| `ViewportProxyModel` | `src/models/viewportproxy.h/cpp` | 视窗代理：窗口化行映射，虚拟表格核心 |
| `TraceView` | `src/ui/traceview.h/cpp` | 报文列表视图：排序、多选标记、导航、导出、列布局 |
| `TraceTab` | `src/ui/traceview.h/cpp` | 页面容器：独立 Model + Proxy + 视图组装，Start/Stop 控制 |
| `ViewportOverview` | `src/ui/traceview.h/cpp` | 视窗缩略图：Rx/Tx 密度分布绘制、拖拽导航 |
| `FrameInfoWidget` | `src/ui/traceview.h/cpp` | 帧结构面板：紧凑文本字段显示 |
| `SignalDecodeWidget` | `src/ui/traceview.h/cpp` | 信号解析面板：DBC 解码值显示 |
| `FilterEngine` | `src/core/filter_engine.h/cpp` | 表达式引擎：递归下降解析 `id == 0x123 && dlc > 4`；标识符 id/dlc/ch/time/fd/ext/rx/tx/std/error |
| `BookmarkManager` | `src/core/bookmarkmanager.h/cpp` | 书签管理：文件级导出/导入 ✅（saveToFile/loadFromFile）；工程文件自动集成待实现 |

### 2.3 数据列定义

| 列 | 枚举 | 说明 |
|----|------|------|
| No. | `ColNo` | 帧编号（seqCounter，永不回退，与环形缓冲物理位置解耦） |
| Time | `ColTime` | 时间戳（随 TimestampMode 切换 5 种格式） |
| Delta | `ColDelta` | 与上一帧时间增量 |
| Ch | `ColChannel` | 通道号 |
| Dir | `ColDirection` | Rx / Tx |
| ID | `ColId` | CAN ID（hex，支持 `>`/`<`/`!=` 列过滤运算符） |
| DLC | `ColDlc` | 数据长度码 |
| Data | `ColData` | 十六进制数据 |
| Flags | `ColFlags` | 标志位 |
| Count | `ColFrameCount` | 同 CAN ID 累计帧数 |
| Signal | `ColSignal` | 内联信号值（DBC 解码） |

---

## 三、性能优化方案（当前状态 + 待实施）

### 3.1 瓶颈分析（原始问题）

| # | 瓶颈点 | 影响 | 状态 |
|---|--------|------|------|
| P1 | `data()` 逐次格式化，10万行×10列 = 100万次字符串分配 | 滚动/绘制极慢 | ✅ Phase 1 已解决 |
| P2 | 逐帧 `beginInsertRows`，5000帧/s 触发 5000 次/秒模型布局更新 | UI 线程过载 | ✅ Phase 2 已解决 |
| P3 | `QSortFilterProxyModel::invalidateFilter()` 全量重算 O(n) | 过滤态持续捕获每帧 O(n) 重过滤 | ✅ Phase 3 已解决（增量映射代理） |
| P4 | `recomputeDisplayDeltas` O(n) 遍历 | 模式切换耗时数秒 | ✅ 随 Phase 1 缓存解决 |
| P5 | 着色规则逐行求值（含正则） | 滚动时每个可见行重新匹配 | ✅ Phase 1 缓存 BackgroundRole |
| P6 | `QVector::removeFirst()` 全量内存移动 | 高频场景每帧移动 10 万 CanFrame | ✅ Phase 4 环形缓冲解决 |
| P7 | 无显示刷新率控制 | CPU 占用不受控 | ✅ Phase 2 可调刷新率解决 |

### 3.2 Phase 1 — 延迟格式化 + 可见行缓存 ✅ 已完成

**原理**（借鉴 Wireshark）：`data()` 不每次格式化，以行号为 key 缓存 10 列格式化字符串，仅缓存可见行 ±50 行。

**已实现要点**：
1. `QHash<int, RowCache> m_rowCache` — 每行缓存 `QString cols[ColCount]` + 背景色
2. `data(DisplayRole)` 先查缓存，未命中才 `formatCell()` 并写缓存
3. `setVisibleRange(first, last)` 通知可见区变化，淘汰区外缓存
4. 时间格式切换 / 覆盖模式刷新 / `invalidateRowCache()` 使缓存失效
5. 着色规则求值结果缓存到 `RowCache.bgColor`（含 `bgValid` 有效位）

**收益**：滚动/绘制性能提升 10-50 倍。

### 3.3 Phase 2 — 批量更新 + 可调刷新率 ✅ 已完成

**原理**（借鉴 TSMaster）：逐帧追加改为 pending 队列缓冲 + 定时 flush。

**已实现要点**：
1. `m_pendingFrames` 队列 + `m_flushTimer`
2. `appendFrame()` 只入队；定时器触发 `commitBatch()` 一次性 `beginInsertRows(first,last)` + 批量 append + `endInsertRows()`
3. 刷新率档位：High(50ms) / Medium(100ms) / Low(200ms) / Paused(0)
4. Paused 时数据仍入队，`flushPending()` 恢复时一次性提交
5. 离线批量加载 `appendFrames()` 直通不经队列
6. 提交后发 `framesCommitted(count)` 信号驱动统计刷新

**收益**：5000 帧/s 实时捕获 UI 更新从 5000 次/s 降至 20 次/s，CPU 降低 90%+。

### 3.4 Phase 3 — 增量过滤代理 ✅ 已实施（2026-08-17，`src/models/cantraceproxymodel.h/cpp`）

**原理**：`QSortFilterProxyModel` 在 `invalidateFilter()` 时重评估全部行。自定义代理仅评估新增行，已有行过滤结果通过映射表保留。

**实施方案**：
1. 新建 `CanTraceProxyModel`（不继承 `QSortFilterProxyModel`，替代 `CanFilterProxyModel`）
2. 维护 `QVector<int> m_sourceToProxy` / `m_proxyToSource` 双向映射
3. 新增行：仅评估新行，append 到映射尾部（O(1)/行）
4. 过滤条件变化：单次全量遍历重评估
5. 排序：独立排序索引数组，不动源模型行号
6. `SinceDisplay` Delta 增量计算：新行只与上一显示行比较

**与 ViewportProxyModel 的关系**：Phase 3 实施后代理链变为
```
CanTraceModel → CanTraceProxyModel(增量过滤) → ViewportProxyModel(视窗) → TraceView
```

**收益**：过滤态持续捕获新增行开销 O(n)→O(1)；SinceDisplay 切换 O(n)→O(1)。

**涉及文件**：
- 新增：`src/models/cantraceproxymodel.h/cpp`
- 修改：`src/ui/traceview.h/cpp`（TraceTab 组装新代理链）
- 删除：`src/models/canfilterproxymodel.h/cpp`（不考虑向后兼容，直接替换）

### 3.5 Phase 4 — 环形缓冲区存储 ✅ 已完成

**原理**（借鉴 CANoe）：固定容量数组 + 头尾指针替代 `QVector::removeFirst()`。

**已实现要点**：
1. `RingBuffer<CanFrame> m_ringBuffer`（`src/utils/ringbuffer.h`），默认容量 100 万帧可配置
2. `frameAt(row)` = `(head + row) % capacity`，O(1) 随机访问
3. 满时头指针前进覆盖最旧帧，无需 `beginRemoveRows`
4. `No.` 列返回 `m_seqCounter`（永不回退），与物理位置解耦
5. 标记/着色/标签 key 全部使用 `seqCounter`（`QSet<quint64> m_markedRows` 等），覆盖不错位

**收益**：消除 O(n) 内存移动；百万帧内存可控。

### 3.6 性能目标

| 场景 | 目标 |
|------|------|
| 10 万帧离线加载 | <1s |
| 10 万帧滚动浏览 | 流畅 60fps ✅（Phase 1+视窗层已达成） |
| 5000 帧/s 实时捕获 | 流畅，CPU <20% ✅（Phase 2 已达成） |
| 过滤条件切换（10 万帧） | <500ms |
| 时间格式切换（10 万帧） | <100ms ✅ |
| 百万帧加载 | <5s，内存 <500MB |
| 着色规则应用（10 万帧） | <200ms ✅（缓存生效后） |

### 3.7 验证方法

1. **基准脚本** — 生成 1万/10万/100万帧测试数据（BLF/ASC），加载计时
2. **性能日志** — spdlog 记录 `data()` 调用次数、缓存命中率、flush 耗时
3. **实际场景** — ZLG 设备 5000帧/s 实时捕获观察流畅度与 CPU
4. **回归** — Delta、覆盖模式、着色规则、多选标记、导出不受影响

---

## 四、功能增强规划（待实现）

### 4.1 书签持久化（P1，对标 CANoe Trace 标记）

- 右键行 → 「添加书签」→ 输入备注（当前已有运行时标记 + 行标签）
- 书签结构：`{seqCounter, note, timestamp, color}`
- 持久化到 `.openbusproj` 工程文件（或独立 `.sbm` 文件）
- 右侧面板「书签」标签页：列表点击跳转对应帧
- 现状（2026-08 复查修正）：`BookmarkManager` 文件级 saveToFile/loadToFile ✅、右侧面板书签列表 ✅、右键「跳转到标记」菜单 ✅
- 剩余差距：随 `.openbusproj` 工程自动保存/恢复；加载 Trace 文件时按 seqCounter 匹配恢复

**涉及文件**：
- 修改：`BookmarkManager`（增加 save/load）
- 修改：`src/ui/rightpanel.h/cpp`（书签标签页）

### 4.2 着色规则编辑器 UI（P1，对标 CANoe/Wireshark Coloring Rules）

规则引擎（`ColorRule{expr, background, foreground, enabled}` + 首个命中生效）已实现，缺可视化编辑器：

```
┌─ 着色规则编辑器 ──────────────────────────────┐
│  规则列表                      [添加] [删除]   │
│  ┌──────────────────────────────────────────┐  │
│  │ ▲ id == 0x123        🟡 黄色背景         │  │
│  │   error == true      🔴 浅红背景         │  │
│  │   fd == true         🔵 浅蓝背景         │  │
│  └──────────────────────────────────────────┘  │
│  条件: [id == 0x123                        ]   │
│  背景: [⬛ 黄色 ▼]   前景: [⬛ 黑色 ▼]           │
│                                  [取消] [确定]   │
└────────────────────────────────────────────────┘
```

- 规则持久化到 `.openbusproj`
- 变更后触发 `invalidateRowCache()` 重新着色（仅可见行，<200ms）

**涉及文件**：
- 新增：`src/ui/colorruleeditor.h/cpp`

### 4.3 预定义过滤集管理（P1，对标 CANoe Filter Presets）

- FilterBar 右侧「预设」下拉按钮，点击应用
- 右键当前表达式 → 「保存为预设」
- `.sfilter` 文件（JSON）存储，可导入导出团队共享：

```jsonc
{
  "version": 1,
  "filters": [
    {"name": "仅 Rx 帧",   "expr": "rx"},
    {"name": "仅错误帧",   "expr": "error"},
    {"name": "制动系统",   "expr": "id in (0x100, 0x1A5, 0x2FF)"}
  ]
}
```

**涉及文件**：
- 新增：`src/core/filterpresetmanager.h/cpp`
- 修改：`src/ui/filterbar.h/cpp`

### 4.4 Hex Dump 增强（P2，QHexEdit2）

当前 `QPlainTextEdit` 纯文本显示，引入 **QHexEdit2**（BSD）替换：

| 特性 | 当前 | QHexEdit2 |
|------|------|-----------|
| 查看 | ✅ 文本 | ✅ 高亮文本 |
| 选中/复制 | ❌ | ✅ 区域选中、右键复制 |
| 查找 | ❌ | ✅ Hex/ASCII 双向查找 |
| 信号高亮 | ❌ | ✅ DBC 信号位着色标注 |

### 4.5 统计面板增强（P2）

- 总线负载率：`(Σ(47 + DLC×8) bits) / (时长 × 波特率) × 100%`
- 各 ID 帧数/频率表格（`BusStatistics` 已有基础）
- 周期抖动分析：均值/最大/最小/标准差 σ
- 错误帧分类统计（Stuff/Form/ACK/Bit0/Bit1/CRC）

---

## 五、交互设计记录（已实施的用户反馈）

以下反馈均已实现，作为设计约束保持：

1. **视窗滑动条放左侧**，视窗大小高亮块在滑动条上可拖拽；视窗内常规滑动条在右侧
2. **覆盖模式入口**收纳到搜索框右侧设置按钮的内部选项，默认不开启
3. **多选标记**：Ctrl 选任意多行、Shift 选连续多行，选中后右键标记
4. **虚拟表格**：通过 `ViewportProxyModel` 视窗层实现，克服大数据卡顿

已修复问题：
- 时间显示格式切换后排序仍按旧格式 → 已修复（排序键改用原始数值）

---

## 六、Trace 专用开源组件清单

| 组件 | 协议 | 用途 | 状态 |
|------|------|------|------|
| vector_blf | LGPL | BLF 解析 | ✅ 已集成（自研替代实现，绕过 LGPL 传染） |
| 自写 ASC 解析器 | — | ASC 导入/导出（~300 行） | ✅ 已实现 |
| 自写 CSV 解析器 | — | CSV 导入/导出（~200 行） | ✅ 已实现 |
| QHexEdit2 | BSD | Hex 查看控件（选中/查找/信号高亮） | ⬜ 待引入 `third_party/qhexedit2/` |
| moodycamel::ConcurrentQueue | BSD | 线程安全帧队列 | ✅ 已集成 |
| spdlog | MIT | 性能日志 | ✅ 已集成 |

**选型原则**：
1. 优先 MIT/BSD，规避 GPL 传染
2. Trace 表格强制虚拟 Model（`QAbstractItemModel`），禁止 QTableWidget 加载大量数据
3. 不引入新重型依赖，Phase 3 基于Qt6 原生实现

---

## 七、实施计划

| 阶段 | 内容 | 优先级 | 预计工时 | 来源 |
|------|------|--------|---------|------|
| **T1** | ✅ Phase 3 增量过滤代理（`CanTraceProxyModel` 替换 `CanFilterProxyModel`） | 🔴 高 | ~~3-4 天~~ 完成 | §3.4 |
| **T7** | ✅ 速赢包：Name 列 + 下一/上一标记导航 + 字体缩放 + 默认语义配色 | 🔴 高 | ~~1-2 天~~ 完成 | §九 G-F3/G-F8/G-F10/G-U4 |
| **T2** | 书签接入工程文件（文件级导出导入已实现，仅差工程自动集成） | 🟡 中 | 0.5-1 天 | §4.1 |
| **T8** | ✅ Trace Explorer 底部标签页外壳（详情/信号/统计/差异四标签） | 🟡 中 | ~~1 天~~ 完成 | §九 G-U1 |
| **T9** | ✅ 选中帧统计视图（时间 Δt + 信号/字节 min/max/avg/σ/首末） | 🟡 中 | ~~2-3 天~~ 完成 | §九 G-F1 |
| **T10** | ✅ 选中帧差异对比视图（概览/字节级/信号级首末对比） | 🟡 中 | ~~2-3 天~~ 完成 | §九 G-F2 |
| **T3** | ✅ 着色规则编辑器 UI（2026-08 复查确认已实现，含 QSettings 持久化） | 🟡 中 | ~~1-2 天~~ 完成 | §4.2 |
| **T4** | 预定义过滤集管理（升级：层次树 + 眼睛开关） | 🟡 中 | 2 天 | §4.3 + §九 G-F6 |
| **T11** | 结构化详情树（字段分组 + DBC 值表释义） | 🟢 低 | 2 天 | §九 G-U2 |
| **T12** | 图标工具栏 | 🟢 低 | 1-2 天 | §九 G-U3 |
| **T13** | Pass/Stop 双过滤组 | 🟢 低 | 1-2 天 | §九 G-F7 |
| **T5** | QHexEdit2 集成 + DBC 信号高亮 | 🟢 低 | 2-3 天 | §4.4 |
| **T6** | 统计增强（周期抖动 + 错误分类） | 🟢 低 | 2-3 天 | §4.5 |
| 暂缓 | 行内展开 / Marker 条 / 覆盖淡出 / 混合事件流 | ⚪ 规划 | — | §九 G-F4/G-F9 等 |

依赖关系：T1、T7 独立可先行；T8 外壳先行，T9/T10 依赖 T8 容器；T2/T3/T4 相互独立可并行；G-F9 混合事件流挂接插件 v2 协议冻结后统一设计。实施批次建议见 §9.4。

---

## 八、设计约束

1. **不考虑向后兼容**：Phase 3 直接删除 `CanFilterProxyModel`，用新代理替换
2. **数据一致性**：标记/着色/标签必须以 `seqCounter` 为 key（环形缓冲覆盖安全）
3. **缓存失效纪律**：任何影响显示输出的变更（时间格式、着色规则、覆盖刷新）必须调用 `invalidateRowCache()`
4. **UI 线程纪律**：禁止在 `data()` 中做 O(n) 操作；新帧只入 pending 队列
5. **多实例独立**：每个 TraceTab 拥有独立的完整代理链，互不共享状态

---

## 九、CANoe 深度对标差距分析（2026-08 复查）

### 9.1 复查方法与结论

逐项核对 CANoe Trace Window 工具栏与视图能力（Detail / Statistics / Difference / Predefined Filter / Analysis Filter / Search / Display Mode / Time Mode / Marker / Column Layout / Font Size / Trace Configuration），并逐条对照 openbus 源码验证实现状态（非仅凭本文档历史记录）。

**复查修正**（此前文档漏记，源码确认已实现）：
1. 列筛选值勾选面板 — `ColumnFilterPopup`（搜索框 + 全选/反选 + 值计数），达 CANoe 同级
2. 时间显示精度 — `setTimePrecision(-1/0/3/6/9)` + 设置菜单入口
3. 书签文件导出/导入 — `BookmarkManager::saveToFile/loadToFile` + 右侧面板书签列表
4. 标记跳转菜单 — 右键「跳转到标记」子菜单；错误帧高亮开关

**核心结论**：列表性能、导航、列筛选已达或超出竞品；剩余差距集中在三组：

- **A. 选中帧分析视图**（统计 / 差异对比）— CANoe 高频分析工作流，openbus 完全缺失 → **最高优先**
- **B. 信息呈现结构**（Name 列 / Explorer 标签页 / 结构化详情）→ **高优先**
- **C. 过滤与事件体系完整度**（预设层次树 / Pass-Stop / 混合事件流）→ 中低优先 / 战略项

### 9.2 功能特性差距清单

| 编号 | 能力 | CANoe 行为 | openbus 现状 | 优先级 |
|------|------|-------------|--------------|--------|
| G-F1 | 选中帧统计视图 | 选中若干帧 → Statistics 视图：Δt min/max/avg、帧数、各信号 min/max/avg | ✅ 已实现（2026-08-17：TraceStatisticsWidget，时间/信号/字节三组统计 + σ 增强，100ms 防抖 + 万帧截断） | ✅ 完成 |
| G-F2 | 选中帧差异对比 | 选中 ≥2 帧 → Difference 视图：首末 Δt、信号首末值、变化项突出 | ✅ 已实现（2026-08-17：TraceDiffWidget，概览/字节级高亮/信号级首末对比 + 仅变化项开关） | ✅ 完成 |
| G-F3 | DBC 报文名称列 | Name 列显示 DBC 报文名 | ✅ 已实现（2026-08-17：ColName + 过滤/排序/列布局 v2） | ✅ 完成 |
| G-F4 | 行内信号展开 | 行首 + 号展开该帧全部信号解码 | ❌ 底部面板单帧显示（G-U2 增强后可替代） | ⚪ P3 暂缓 |
| G-F6 | 预定义过滤器层次树 | 分类树（Bus Systems/Variables/…）+ 每类眼睛开关即时启停 | ⬜ §4.3 规划为平面预设列表 | 🟡 P2（并入 T4） |
| G-F7 | Pass/Stop 双过滤组 | Pass filter（白名单）+ Stop filter（黑名单）可多组，右键/拖帧入组 | ❌ 单一主表达式可模拟，无 UI | 🟢 P2 |
| G-F8 | 标记顺序导航 | Go to/Next/Previous Marker 快捷键 + Marker bar | ✅ 键盘导航已实现（Ctrl+./Ctrl+, + 右键菜单）；标记条仍缺（ViewportOverview 部分覆盖） | ✅ 导航完成（2026-08-17）/ ⚪ P3（标记条） |
| G-F9 | 混合事件流 | 同一流显示 CAN/错误事件/系统变量/诊断服务 | ❌ 仅 CAN 帧；UDS 独立页面 | ⚪ P3 战略 |
| G-F10 | 字体大小调节 | 工具栏 A+/A- | ✅ 已实现（Ctrl+滚轮 / Ctrl+±、Ctrl+0 / 右键菜单 + QSettings 持久化，行高随字号自适应） | ✅ 完成（2026-08-17） |
| G-F12 | 覆盖模式陈旧行处理 | 固定模式非周期事件陈旧淡显 + Clear faded events 清理 | ❌ 覆盖模式无超时淡出/清理 | ⚪ P3 |

#### G-F1 选中帧统计视图（→ T9）✅ 已实现（2026-08-17）

- 入口：工具栏「统计」图标；数据源 = 当前选中行集合（`TraceView::selectedSourceRows()` 已有）
- 内容两组：
  - **时间**：帧数 N、相邻帧 Δt 的 min / max / avg / σ（标准差，CANoe 无 σ，此处增强）
  - **信号**：经 DbcManager 解码后逐信号 min / max / avg / 首值 / 末值；无 DBC 时按字节位置统计
- 实现：选中变化防抖 100ms 后重算；集合 > 10000 帧时截断并提示
- 涉及：`src/ui/tracestatisticswidget.h/cpp` ✅；挂载于 Trace Explorer「统计」标签（T8），选中集合由 `TraceView::selectedSourceRows()` 喂入

#### G-F2 选中帧差异对比视图（→ T10）✅ 已实现（2026-08-17）

- 入口：选中 ≥2 帧 →「差异」图标；恰好 2 帧时退化为 A/B 对比
- 内容（对标 CANoe 首末对比并增强）：
  - 首帧/末帧 Δt
  - **字节级**：双列 Hex 对照，不等字节高亮
  - **信号级**：逐信号 首值 → 末值，默认仅列变化项（CANoe 同款），可切换列全部
- 涉及：`src/ui/tracediffwidget.h/cpp` ✅，复用 DbcManager 解码；挂载于 Trace Explorer「差异」标签（T8）

#### G-F3 DBC 报文名称列（→ T7）✅ 已实现（2026-08-17）

- `ColName` 枚举插入 ID 列之后；`DbcManager` 按 id 查报文名，未命中留空
- RowCache 现有机制直接生效，零额外性能成本；列布局持久化默认版本 +1
- 工时 ≈ 0.5 天

#### G-F7 Pass/Stop 双过滤组（→ T13）

- FilterBar 升级为双 chip：Pass（绿，白名单）与 Stop（红，黑名单），各自独立表达式 + 启用开关
- 合成规则：`passExpr && !(stopExpr)`（FilterEngine 已支持 `!` 与 `&&`，引擎零改动）
- 右键帧 →「加入 Pass 过滤 / 加入 Stop 过滤」（按 `id == 0xXXX` 追加 OR 项）
- 涉及：`src/ui/filterbar.h/cpp`、TraceView 右键菜单

#### G-F8 标记顺序导航（→ T7）✅ 导航已实现（2026-08-17）

- TraceView 新增动作「下一个/上一个标记」（Ctrl+. / Ctrl+,，与 Ctrl+Down/Up 的同 ID 跳转区分），导航范围为标记/着色/标签行（labeledMarks）
- 实现：遍历 CanTraceModel 标记集合（seqCounter 有序），`selectSourceRow()` 跳转
- 标记条（P3）：ViewportOverview 右侧 8px 窄条绘制标记刻度，点击跳转（复用其密度绘制经验）

#### G-F9 混合事件流（战略项，暂缓）

- 方向：CanTraceModel 泛化为 TraceEventModel，事件携带类型标签（CAN 帧 / 错误事件 / 用户事件 / 插件诊断输出），列布局按事件类型差异化（对标 CANoe Column Layout per event type）
- 与插件 v2 `dataOutputs.diagnostics` 天然汇合 —— 待插件协议冻结后统一设计，避免双轨
- 近期不做，不阻塞 T1-T6

### 9.3 UI 风格差距清单

| 编号 | 风格项 | CANoe | openbus 现状 | 建议 |
|------|--------|-------|--------------|------|
| G-U1 | 分析视图组织 | 左侧 Trace Explorer：Detail/Statistics/Difference/Filter 可切换视图 | ~~底部静态双面板（帧结构/信号解析，QPlainTextEdit 纯文本）~~ | ✅ 已实现（2026-08-17，T8 外壳：底部标签式 Trace Explorer 详情/信号/统计/差异四标签；详情/信号仍为纯文本，结构化详情见 T11） |
| G-U2 | 详情结构化呈现 | Detail 按协议层分组、字段名-值-释义、可复制 | 纯文本逐行 | 改 QTreeWidget 字段树：报文头/数据/信号分组 + DBC 值表释义列（→ T11） |
| G-U3 | 图标工具栏 | 暂停/详情/统计/差异/过滤器/搜索/清除/显示模式/时间模式/字号 一排图标 | 以设置下拉菜单为主 | 紧凑图标工具条 + tooltip；设置菜单保留高级项（→ T12） |
| G-U4 | 默认语义配色 | Rx/Tx/错误成套默认配色体系 | ✅ 已实现（2026-08-17：首次运行自动安装 error 淡红 / Tx 淡蓝默认着色规则；远程帧因 CanFrame 无 RTR 位不做） | ✅ 完成（用户可在着色规则编辑器中覆盖） |
| G-U5 | 双缓冲区分析区导航 | main buffer + analysis area 时间滑条 | —（架构不同） | **不做**：ViewportOverview 缩略图 + 标记导航已覆盖同类需求 |

### 9.4 实施建议与路线

新增项已合并入 §七 实施计划（T7-T13）。批次建议：

1. **第一批（性能 + 速赢，约 1 周）**：✅ 已完成（2026-08-17）：T1 增量过滤代理 + T7 速赢包（Name 列 / 标记导航 / 字体缩放 / 默认配色），另附 FilterEngine 新增 `error` 标识符
2. **第二批（分析工作流，1-2 周）**：✅ T8/T9/T10 已完成（2026-08-17：Trace Explorer 标签外壳 + 选中帧统计视图 + 差异对比视图）；T2 书签工程集成 / T4 预定义过滤集未开始（T3 已完成）
3. **第三批（体验完善）**：T11 结构化详情、T12 工具栏、T13 Pass/Stop、T5 QHexEdit2、T6 统计增强
4. **战略项**：G-F9 混合事件流，待插件 v2 协议冻结后与 dataOutputs 通道统一设计
