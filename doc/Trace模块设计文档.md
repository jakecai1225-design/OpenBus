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

> **2026-08-21 增补**：多协议 Trace 形态扩展方向（**§十**）——侧栏 Trace 当前仅一种形态（CAN 帧列表，多实例），面向市面常见协议调研归纳出六种基础视图形态（帧列表 / 事务配对 / 聚合监视 / 文本日志流 / 字节流 / 时序段），作为战略扩展方向，与 doc/flow.md 多协议 Flow 方案配套。

> **2026-08-21 增补（二）**：最小改动预埋（**§10.7**）——M 系列（doc/flow.md §十三）的 Trace 切片 M1-Trace：形态模板平铺（v1.5 修订）+ formId/protocolId 身份预埋，可在 TR1 之前先行落地，数据层零改动。

> **2026-08-23 增补（三）**：侧栏版式统一 VS Code Explorer 垂直序——「已打开」区**置顶**（对标 OPEN EDITORS），新建模板行与操作按钮（删除）**收拢到面板下方**；§10.4 / §10.7 图示与描述同步（详见 doc/flow.md §7.2 版式补记）。

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
| **TR 系列** | 多协议 Trace 形态（TR1 形态框架 → TR2 聚合监视 → TR3 事务配对 → TR4 文本·字节流 → TR5 时序段 → TR6 混合事件流） | ⚪ 战略规划 | — | §十 |
| **M1-Trace** | 形态模板平铺（帧列表可点击，五形态置灰占位）+「已打开」实例列表 + formId/protocolId 身份预埋（flow.md §十三 M1 的 Trace 切片，TR1 公共前置） | 🟡 近期 | ~0.5 天 | §10.7 |

依赖关系：T1、T7 独立可先行；T8 外壳先行，T9/T10 依赖 T8 容器；T2/T3/T4 相互独立可并行；G-F9 混合事件流挂接插件 v2 协议冻结后统一设计；**M1-Trace 独立可先行**（随 flow.md M1 落地，无组件依赖）。实施批次建议见 §9.4。

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
4. **战略项**：G-F9 混合事件流，待插件 v2 协议冻结后与 dataOutputs 通道统一设计（已收编入 §十 TR6 多形态路线）

---

## 十、多协议 Trace 形态扩展方向（战略规划，尚未实施）

> 2026-08-21 增补。现状：Trace 侧栏（`TracePanel`）点开后的折叠列表只支持**一种** Trace
> 形态——CAN 帧列表（可多实例，`onOpenTraceTab` 每次新建一个同构 `TraceTab`）。但"一行
> 一帧"的帧列表并不适配所有协议的分析工作流：诊断协议关心请求-响应**配对**，串口数据是
> **文本流**，周期过程数据更适合"**最新值**"聚合视角。本节调研市面常见协议的 Trace 形态，
> 归纳为六种基础形态，作为后续扩展方向。与 doc/flow.md 互为配套：Flow 解决「数据从哪来、
> 怎么解析」（BusMessage / 适配器 / 解析器），本节解决「以什么形态看」。

### 10.1 现状与差距

- **现状**：`TracePanel`（sidebarpanels.cpp:700）= Trace 实例列表 + 「新建 Trace」按钮；
  每个实例是同构的 `TraceTab`（帧列表形态，§2.3 的 12 列），多实例相互独立。
- **形态被焊死在控件链上的位置**：
  - `CanTraceModel` 的 12 列枚举（ColNo…ColName）全部是 CAN 帧语义
  - `TraceTab` 组装（FilterBar + TraceView(QTableView) + ViewportOverview）整链面向"一行一帧"
  - UDS 诊断走独立 `UdsView` 页面，不进 Trace 流（§九 G-F9 已指出）
- **差距**：**形态**（怎么呈现）与**协议**（数据是什么）耦合在一个控件链里。flow.md 的
  `traceColumns()` 只解决**列差异**（同一形态内换列）；换**形态**（诊断事务配对、文本流、
  聚合监视）无处安放。列 × 形态是两个正交维度。

### 10.2 市面协议 × Trace 形态调研

| 协议 | 市场工具与 Trace 形态 | 关键列 / 字段 | 归纳形态 |
|------|----------------------|--------------|---------|
| CAN / CAN FD | CANoe Trace、PCAN-View：一行一帧 | No./Time/Ch/Dir/ID/DLC/Data/Flags（= 现有 12 列） | ① 帧列表型 |
| SAE J1939 | CANoe J1939 Trace：PGN 视角 | PGN/SA/DA/优先级；TP 多包（BAM/RTS-CTS）重组为一条 | ① + 事务重组 |
| LIN | CANoe LIN Trace | PID/校验和类型/从机响应时间/调度表槽位 | ① 变体 + ⑥ 时序段型 |
| FlexRay | CANoe FlexRay Trace | Slot/Cycle/通道 A/B/静态·动态段 | ① 变体 + ⑥ 时序段型 |
| CANopen | CANoe CANopen Trace | SDO 请求-响应配对（Index/Subindex）、NMT/心跳 | ② 事务配对型 |
| UDS（ISO 14229） | CANoe Diagnostics、各诊断工具 | 服务配对（请求-响应/超时/NRC）、ISO-TP 多帧重组 | ② 事务配对型 |
| Modbus RTU/TCP | Modbus Poll、Wireshark | 功能码/寄存器地址与值/异常码，一问一答 | ② 事务配对型 |
| SOME/IP · DoIP | Wireshark | 源/目的 IP:端口、协议分层树、UDP/TCP 流重组 | ① + 分层详情树（T11） |
| EtherCAT | acontis EC-Engineer、TwinCAT | 过程映像（周期数据最新值）+ 邮箱 CoE 事务 | ③ 聚合监视 + ② 事务配对 |
| 串口 / UART（ASCII） | 串口助手、Serial Studio | 时间戳 + 文本行滚动日志 | ④ 文本日志流型 |
| 通用二进制流 | Hex 查看 / 协议分析工具 | 时间戳 + 连续字节 Hex dump | ⑤ 字节流型 |
| 系统事件 / 插件输出 / 变量 | CANoe 混合事件流 Trace | 事件类型标签 + 按事件类型差异化 Column Layout | ① 混合事件流（= §九 G-F9） |

**观察结论**：

1. CANoe 的做法是**按总线/协议提供专属 Trace 窗口**（CAN / LIN / FlexRay / 诊断各有 Trace
   变体）——形态跟着协议的分析工作流走，而非一种 Trace 通吃。
2. Wireshark 反其道：**一套分组列表 + Protocol/Info 列 + 分层详情树**统一所有协议，代价是
   详情树承担全部协议差异（对应本方案 T11 结构化详情 + flow.md `decode()` 冷路径）。
3. openbus 路线 = 两者结合：**形态泛化（本节）+ 协议差异下沉到适配器（flow.md）**。

### 10.3 六种基础形态抽象

| # | 形态 | 一行代表 | 典型协议 | 与现有架构的关系 |
|---|------|---------|---------|----------------|
| ① | 帧列表型 Frame List | 一帧 / 一分组 | CAN、车载以太网、混合事件流 | **现有形态**：Phase 1-4 全链路（环形缓冲/批量提交/增量过滤/视窗代理）直接复用 |
| ② | 事务配对型 Transaction | 一组请求-响应（含 pending/超时/NRC 状态） | UDS、CANopen SDO、Modbus、CoE 邮箱 | 帧列表之上加**事务装配层**（TransactionAssembler：配对窗口 + 超时状态机），子行可展开回原始帧 |
| ③ | 聚合监视型 Aggregated Watch | 一个报文 ID / 信号，最新值原地更新 | EtherCAT 过程映像、CAN 周期报文监视 | 现有**覆盖模式**（同 ID 留最新，§1.1）是其退化形式；数据源复用 DBC 解码路径 |
| ④ | 文本日志流型 Text Log | 一行文本 | 串口 ASCII、插件输出、系统事件 | QPlainTextEdit + 最大行数环形 + 追加节流，**不走虚拟表格链** |
| ⑤ | 字节流型 Byte Stream | 一个带时间戳的数据块 | 通用二进制、串口 HEX 模式 | Hex dump 分块渲染（QHexEdit2，T5 引入后复用），可选 ASCII 列 |
| ⑥ | 时序段型 Timeline | 一段时间轴 | LIN 调度表、FlexRay 周期、总线占用 | 远期；与 Graphic 时间轴共用坐标体系 |

**设计约束**：

1. **性能地基共享**：①②③ 复用「环形缓冲 + 批量提交 + 刷新率节流」（Phase 2/4）；④⑤
   复用「追加 + 节流」纪律但渲染控件不同；视窗代理（ViewportProxyModel）仅虚拟表格形态
   需要。Phase 1-4 的性能投资在新形态下不浪费。
2. **形态与协议解耦**：形态声明典型适用协议，但同一协议可开多种形态实例（CAN 也能开
   聚合监视型）；协议可声明默认形态。
3. **过滤引擎按形态裁剪标识符集**：现有标识符（id/dlc/ch/…，§2.2）是帧列表形态的方言；
   事务型增加 `service/nrc/state`，文本型仅 `time/content`。
4. **多实例独立原则不变**（§八 约束 5）：每个形态实例拥有独立数据链与过滤上下文。

### 10.4 架构方向

侧栏按**形态**平铺模板入口（与 flow.md §7.2 v1.5 平铺修订一致，不再分节嵌套；
现状的单列表收编为「已打开」区，新建按钮改为模板行；版式补记 2026-08-23：
已打开置顶、新建模板与操作按钮收拢面板下方，VS Code Explorer 垂直序）：

```
┌─ Trace（侧栏面板） ────────────────────────────┐
│  ── 已打开 ──                                   │
│  ● Trace1         [CAN]  ▶ 运行中              │
│  ● Trace2         [CAN]  ⏸ 已暂停              │
│  ── 新建 Trace ──                                │
│  ＋ 帧列表      （可点击：新建帧列表 Trace）    │
│  ＋ 事务配对    （置灰占位，TR 系列落地启用）   │
│  ＋ 聚合监视    （置灰占位）                    │
│  ＋ 文本日志流  （置灰占位）                    │
│  ＋ 字节流      （置灰占位）                    │
│  ＋ 时序段      （置灰占位）                    │
│  [－删除]      ← 底部操作                       │
└────────────────────────────────────────────────┘
```

交互沿用 Flow 侧栏规则（flow.md §7.2）：单击模板行创建该形态新实例（置灰占位行
不响应，tooltip 标注落地阶段）；「已打开」实例行单击聚焦标签页、右键重命名/删除。

组件与接口（方向性设计）：

- **TraceForm 注册表**：`ITraceForm` 接口（formId / displayName / icon / 典型协议 /
  createWidget()）+ `TraceFormRegistry`，与 flow.md 的 `ProtocolRegistry` 同构同址
  （openbus_data，内置 ①-⑥，插件可扩展）。
- **协议适配器尾部追加**（ABI 只增不改，对齐 flow.md §1.3-1）：`IProtocolAdapter` 增加
  `preferredTraceForms()` / `defaultTraceForm()`；`traceColumns()`（flow.md §5.1）继续
  服务帧列表型 / 聚合型的列差异。
- **TraceTab 泛化**：tab = 形态实例 + 列定义 + 独立过滤上下文；Trace Explorer 底部标签
  （详情/信号/统计/差异）在事务配对型下天然适配「子行展开回原始帧」。
- **事务装配层**（② 的关键增量）：在数据链上插入 TransactionAssembler（配对窗口 / 超时
  状态机 / 事务分组），与过滤代理正交可组合。
- **混合事件流汇合（收编 G-F9）**：多形态框架落地后，混合事件流 = 帧列表形态的多协议
  实例 + 事件类型标签 + 按事件类型差异化 Column Layout（对标 CANoe）；「与插件 v2
  `dataOutputs` 统一设计」的既有结论不变。
- **数据入口依赖**：全部依赖 flow.md 的 `invoke("onBus", BusMessage)` 统一入口与
  `bus_type` 分流——**本节是 flow.md F1 之后的下游消费方**，不独立先行。

### 10.5 实施路线（TR 系列，远期）

| 阶段 | 内容 | 依赖 |
|------|------|------|
| TR1 | 形态框架：ITraceForm / TraceFormRegistry + Trace 侧栏模板行由注册表枚举生成（接管 M1 平铺占位） + 帧列表型收编为内置形态（CAN 零回归验收） | flow.md F1 |
| TR2 | 聚合监视型：CAN 周期报文最新值表（覆盖模式的推广 + 信号解码列，与 §4.5 统计协同） | TR1 |
| TR3 | 事务配对型：TransactionAssembler + UDS/ISO-TP 服务配对（与 `UdsView` 融合评估） | TR1 + 诊断模块深化 |
| TR4 | 文本日志流型 + 字节流型：串口 / TCP / 文件源（通用 Flow 场景，QHexEdit2 复用 T5 成果） | flow.md F2 |
| TR5 | 时序段型：LIN 调度表 / FlexRay 周期视图（与 Graphic 时间轴共用坐标体系） | LIN / FlexRay 协议接入 |
| TR6 | 混合事件流：多协议同流 + 事件类型差异化列（收编 §九 G-F9） | 插件 v2 协议冻结 |

### 10.6 与既有规划的关系

| 既有项 | 关系 |
|--------|------|
| doc/flow.md | 上下游配套：Flow 管「数据怎么来、怎么解析」，本节管「以什么形态看」；flow.md §九 openbus_trace 行落地 `traceColumns()` 时同步预留形态接口（flow.md §十二 已互链） |
| doc/flow.md §3.5 角色管线 | Trace `FilterEngine` + `CanTraceProxyModel` = 管线中的**视图级过滤**（只影响显示，不影响环形缓冲/录制）；**流级过滤**（影响录制/缓冲）属 FlowSession 过滤链（F1 字段/F2 实施），FilterEngine 泛化到 BusMessage 后两级复用 |
| Graphic模块设计文档.md §十一 | 同构的形态框架（ITraceForm / IGraphicForm 同注册模式），侧栏模板平铺交互一致；Trace ③ 聚合监视与 Graphic ⑤ 仪表盘同源不同皮（最新值监视的表格皮 / 图形皮） |
| §九 G-F9 混合事件流 | 收编为 TR6，触发条件（插件 v2 协议冻结）不变 |
| §1.1 覆盖模式 | 聚合监视型（③）的退化形式：固定模式非周期事件只留最新一行 |
| §4.5 统计面板 | 聚合监视型的伴生视图（同数据源分视角） |
| §七 实施计划 | TR 系列列为战略项，不占 T1-T13 近期批次 |

### 10.7 最小改动预埋（M1-Trace，对接 flow.md §十三）

多形态框架（TR1-TR6）之前，先落 **M1 的 Trace 切片**（flow.md §十三 M1，约 0.5 人日）——
UI 先行预留、数据层零改动，作为 TR1 的公共前置（v1.5 修订：模板平铺，不再分节嵌套）：

- **形态模板平铺**：`TracePanel` 顶部直接平铺六形态模板入口一行一个——「帧列表」
  可点击（= 新建 Trace，行为同原「新建」按钮）；其余五形态（事务配对 / 聚合监视 /
  文本日志流 / 字节流 / 时序段）置灰占位，tooltip 标注 TR 系列规划——TR1 落地
  `TraceFormRegistry` 后模板行由注册表枚举自动扩充，UI 不再返工。
- **已打开实例列表**：现有实例列表收编为面板顶部「已打开」区（版式补记
  2026-08-23：已打开在上、模板与删除按钮在下），切换 / 右键 / 删除交互原样保留。
- **身份预埋**：TraceTab 尾部追加 `protocolId`（默认 "can"）与 `formId`（默认
  "framelist"）字段；工程持久化 JSON 写入新字段，读取缺省回填（旧工程兼容）。
- **明确不做**：CanTraceModel 列枚举 / 代理链 / FilterEngine / Trace Explorer 全部不动；
  `traceColumns()`（flow.md M2 落地）仅供注册表输出，不接 Trace UI。

**验收**：① CAN Trace 全功能回归（12 列 / 过滤 / 卡尺导航 / 统计差异视图）② 侧栏平铺
呈现六形态模板入口（帧列表可用，其余置灰）+「已打开」实例列表 ③ 旧工程恢复后身份
字段回填正确。

**与 TR1 的交接**：TR1 落地 ITraceForm / TraceFormRegistry 时，仅需把 M1 的硬编码
模板行替换为注册表枚举 + `createWidget()` 分发——平铺结构与交互模式已在 M1 定型，
TR1 不再动侧栏布局。

**已实施（2026-08-21）**：M1-Trace 切片已随 flow.md §十三落地——TracePanel 形态模板
平铺（帧列表可点击新建；事务配对 / 聚合监视 / 文本日志流 / 字节流 / 时序段置灰占位，
`Qt::NoItemFlags`）、「已打开」实例列表（切换 / 右键 / 删除原样保留）、TraceTab
`protocolId`/`formId` 身份字段与工程持久化回填均已实施（见上文「身份预埋」）；
构建通过、`test_protocol` 9/9、全量 ctest 8/9（唯一失败为既有 canfileio BLF roundtrip
用例，与本次改动无关）。**v1.5 修订（同日）**：初版为「折叠分节 + 新建下拉」方案
（CollapsibleSection 组件），产品评审后改为模板平铺，组件移除。实现取舍详见
flow.md §13.2 落地记录。


---

# [MERGED FROM] 离线分析ASC兼容与工程现场还原方案.md

> Merged into this topic doc on 2026-08-26. Full original available in git history.

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
