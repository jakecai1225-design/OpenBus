# Graphic 模块设计文档

> Graphic 是信号级分析的核心视图，对标 CANoe Graphics Window。
> 目标：多信号实时波形监控 + 游标精确测量 + 大数据流畅渲染。

---

## 一、定位与对标

### 1.1 竞品对标分析（CANoe Graphics Window）

| 对标能力 | CANoe Graphics | openbus 状态 |
|---------|----------------|-------------|
| 多信号波形叠加 | ✅ | ✅ 已实现（每信号独立 AxisRect 垂直堆叠） |
| 独立 Y 轴 | ✅ 每信号独立 Y 轴，共享 X 轴 | ✅ 已实现（`QCPAxisRect` × N + 共享时间 X 轴） |
| 单游标测量 | ✅ 垂直线 + 插值取值 | ✅ 已实现（`QCPItemStraightLine` + `valueAtTime()` 线性插值） |
| 双游标差值 | ✅ ΔT / ΔY / 频率 | ✅ 已实现（ΔT、ΔY、f=1/ΔT 信息面板） |
| 信号列表联动 | ✅ 名称/原始值/物理值/单位，随游标刷新 | ✅ 已实现（QTreeWidget 5 列，游标移动实时刷新） |
| 信号显隐切换 | ✅ | ✅ 已实现（复选框 + 自动重布局） |
| 时间窗控制 | ✅ | ✅ 已实现（时间窗下拉 + 适应窗口） |
| 暂停/继续 | ✅ | ✅ 已实现 |
| 当前时间指示线 | ✅ 实时测量位置 | ✅ 已实现（实时跟随最新帧） |
| 视口降采样 | ✅ 大数据抽稀保轮廓 | ⬜ 未实现（当前 50000 点硬截断） |
| 信号数学运算 | ✅ +-*/、表达式派生信号 | ⬜ 未实现 |
| 多窗口游标联动 | ✅ | ⬜ 未实现 |
| 导出图片 | ✅ | ✅ 已实现 |
| 离线文件加载 | ✅ | ✅ 已实现（拖放 BLF/ASC/CSV） |

### 1.2 TSMaster 对标

| 能力 | TSMaster Graphics | openbus 状态 |
|------|-------------------|-------------|
| 多通道叠加 | ✅ | ✅ |
| 缩放平移 | ✅ | ✅ |
| 多 Y 轴 | ✅ | ✅（超出 TSMaster） |
| 游标联动 | ✅ | ⬜ 待实现 |

---

## 二、当前架构

### 2.1 视图布局

```
┌─────────────────────────────────────────────┐
│ 工具栏: 缩放 | 适应 | 采样点 | 单卡尺 | 双卡尺 | 清除 | 暂停 | 导出 │
├──────────┬──────────────────────────────────┤
│ 信号列表  │  波形区 (多轴垂直堆叠)           │
│ 名称      │  ┌─ Signal A (独立 Y 轴) ──┐    │
│ 原始值    │  ├─ Signal B (独立 Y 轴) ──┤    │
│ 物理值    │  ├─ Signal C (独立 Y 轴) ──┤    │
│ 单位      │  └── 共享 X 轴 (时间) ──────┘    │
│ CAN ID   │     卡尺线 (可拖动)              │
│ [显隐]   │     当前时间指示线 (实时)        │
├──────────┴──────────────────────────────────┤
│ 卡尺信息: ΔT = 2.35s  ΔY = 15.3  f = 0.43Hz │
│ 状态栏: 信号数 / 点数 / 时间窗 / FPS         │
└─────────────────────────────────────────────┘
```

### 2.2 类设计（`src/ui/graphicview.h/cpp`）

```cpp
class GraphicView : public QWidget {
    struct Signal {            // 用户配置
        QString name; quint32 canId; bool extended;
        QColor color; DbcSignal dbcSig;
    };
    struct SignalData {        // 运行时数据
        Signal config;
        QCPGraph *graph;        // 曲线
        QCPAxis *yAxis;         // 独立 Y 轴
        QCPAxisRect *axisRect;  // 独立轴区
        QCPItemText *nameLabel; // 信号名叠加文本
        double dataMin, dataMax;
    };
    // 卡尺系统
    enum class CursorMode { None, Single, Double };
    QCPItemStraightLine *m_cursor1, *m_cursor2;
    int m_draggingCursor;       // 0=none 1/2=对应卡尺
    // 性能节流
    QTimer m_replotTimer;       // 50ms 批量 replot (20fps)
    QTimer m_valueTimer;        // 200ms 信号列表值刷新
    static constexpr int MAX_DISPLAY_POINTS = 50000; // 显示上限
};
```

### 2.3 多轴堆叠与联动机制

1. **每信号一个 `QCPAxisRect`**，垂直堆叠，共享底部 X 轴（时间）
2. **X 轴同步**：任一 rect 的 `xRangeChanged` → 中央处理函数统一更新其余 rect
3. **动态布局**：`layoutAxisRects()` 在信号增删/显隐切换时重算各 rect 高度与位置
4. **卡尺取值**：`valueAtTime()` 在相邻采样点间线性插值（`QCPGraph::data()->findBegin/findEnd` 高效查找）

### 2.4 数据流

```
CanSimulator/Player/Recorder 帧到达
    │ onFrame(frame)
    ▼
extractRaw(frame) → 按 DbcSignal 位定义提取原始值
    ▼
物理值 = raw × factor + offset
    ▼
graph->addData(timestamp, value)
    ▼ (50ms 定时批量)
m_replotTimer → replot()  ← 20fps 节流
    ▼ (200ms 定时)
m_valueTimer → updateSignalValues()  ← 信号列表刷新
```

---

## 三、待实现功能规划

### 3.1 视口降采样（P0，最高优先级 — 大数据卡顿根因）

**问题**：当前 `MAX_DISPLAY_POINTS = 50000` 硬截断，回放百万帧时：
- 截断丢失早期数据波形
- 50000 点全量重绘仍然卡顿

**方案**（借鉴 QCustomPlot 工程实践）：

```
原始数据 (百万点)
    │
    ▼ 视口范围 [t1, t2] × 每像素取 2 点
降采样 (min/max 分组抽稀)
    │  每像素列保留该列区间内的 min 和 max 两个点
    ▼
显示数据 (≈ 2 × 视口像素宽 ≈ 4000 点)
    │
    ▼ setData()
replot() — 恒定 O(视口宽度) 渲染成本
```

**实施要点**：
1. 新增 `DownsampleStrategy` 类：`minmax`（保轮廓，推荐）/ `avg` / `first` / `decimate`(每 N 取 1)
2. 视口变化（缩放/平移/时间窗切换）触发重算显示数据，原始数据不动
3. 降采样结果按视口缓存，视口未变复用
4. 环形缓冲区配合：原始数据存 `RingBuffer<QCPGraphData>`（容量可配，默认 100 万点/信号）
5. 拖动卡尺取值始终对原始数据插值（非降采样数据），保证测量精度

**收益**：百万点回放渲染成本从 O(n) 降至 O(视口宽)，60fps 恒定；波形轮廓无损。

**涉及文件**：
- 新增：`src/ui/graphic/downsample.h/cpp`
- 修改：`src/ui/graphicview.h/cpp`

### 3.2 信号数学运算（P1，对标 CANoe Math）

**目标**：由已有信号派生计算信号（如 `Torque = EngineRPM × 0.6 / 9549`）。

**设计**：

```
┌─ 添加计算信号 ────────────────────────────┐
│ 表达式: [EngineRPM * 0.6 / 9549         ] │
│         ↑ 内置表达式引擎复用 FilterEngine? │
│ 名称:   [输出轴扭矩 kW]                   │
│ 单位:   [kW]  颜色: [⬛ 自动]              │
└──────────────────────────────────────────┘
```

**实施要点**：
1. 计算信号类型 `Signal::Type = Dbc | Math`，Math 类型带表达式字符串
2. 表达式引擎：引入轻量解析器，变量名为已添加信号名
3. 数据生成：帧到达时先更新 DBC 信号，再对 Math 信号求值（依赖信号在同一帧或缓存最新值）
4. 计算信号同样支持卡尺插值、显隐、独立 Y 轴

**备选**：exprtk（已下载至 third_party，需评估）；或复用 `FilterEngine` 扩展算术求值。

**涉及文件**：
- 修改：`src/ui/graphicview.h/cpp`（Signal 结构 + 求值）
- 新增：`src/ui/mathsignaldialog.h/cpp`

### 3.3 多视图游标联动（P1）

**目标**：多个 GraphicTab 的卡尺时间同步 — 移动任一视图卡尺，其他视图卡尺移动到同一时间点。

**实施要点**：
1. `GraphicView` 增加 `cursorMoved(double time)` 信号
2. `SplitEditorArea`（或 MainWindow）收集所有活动 GraphicView，收到 `cursorMoved` 后调用其他视图 `moveCursor(which, time)`（带阻塞标志避免回环）
3. 联动开关放工具栏（默认开）
4. 回放场景：回放当前位置线也参与联动（Player → 所有 GraphicView 同步当前时间）

**涉及文件**：
- 修改：`src/ui/graphicview.h/cpp`
- 修改：`src/ui/spliteditorarea.h/cpp` 或 `src/ui/mainwindow.cpp`

### 3.4 测量增强（P2）

- **区域统计**：双卡尺区间内 min/max/avg/σ/RMS 计算，结果表显示
- **信号对比模式**：两信号归一化叠加（各自 Y 轴自动缩放到 0-100%）对比趋势
- **注释标记**：在时间轴上添加文字注释点（测量事件记录）

### 3.5 数据管理增强（P2）

- **信号数据导出**：导出选中信号时间序列为 CSV（time, raw, physical）
- **数据缓存持久化**：Graphic 配置（信号列表、颜色、时间窗）保存到 `.openbusproj`，重开恢复
- **最大内存控制**：所有信号共享内存预算（如 500MB），超限自动降采样归档

---

## 四、技术选型

### 4.1 绘图库对比（结论：QCustomPlot ✅ 已采用）

| 库 | 协议 | 优势 | 短板 | 结论 |
|----|------|------|------|------|
| **QCustomPlot** | GPLv2 / 商业购买 | 轻量纯 Qt、多轴、游标、实时流、文档丰富、工业上位机大量在用 | 百万点需自实现降采样；无内置多通道同步游标 | ✅ **已采用**，已深度实现 |
| Qt Charts | GPL / Qt 商业 | 官方原生 | 性能弱、海量实时延迟高、定制难、商用需购 License | ❌ 已弃用（曾用后迁移） |
| Qwt | Qwt License (GPL 兼容) | 科学绘图、游标标尺优秀、实时流好 | UI 老旧、Qt6 适配坑、学习成本高 | ❌ |
| ImGui-Qt | MIT | 超高渲染性能 | 界面全手写、无法 Designer 拖拽 | ❌ 备选 |

### 4.2 QCustomPlot 关键使用约定

1. `QCPAxisRect` 每信号一个，垂直堆叠，手动 `setMinimumHeight()` 防重叠
2. X 轴范围同步用 `xRangeChanged` 信号汇聚到中央 handler
3. 卡尺用 `QCPItemStraightLine`（垂直线）+ 自管理拖拽状态（`m_draggingCursor`）
4. 插值用 `QCPGraph::data()->findBegin()/findEnd()` 高效区间查找
5. replot 必须节流（当前 50ms 定时批量，20fps）
6. `#include <functional>` 与 QMouseEvent 前置声明（扩展时的坑）

### 4.3 性能避坑

1. **降采样先行**：回放几十上百万帧直接渲染必然卡顿，任何新功能不得绕过降采样直接 `addData` 全量
2. **渲染与数据分离**：数据写入与 replot 分离（当前定时器机制保持）
3. **单线程渲染**：QCustomPlot 默认单线程渲染；实时高流速需数据缓存与 UI 线程分离（pending 队列 + 定时 flush，同 Trace Phase 2 模式）

---

## 五、性能目标

| 场景 | 当前 | 目标 |
|------|------|------|
| 10 信号 × 50000 点实时滚动 | 流畅（截断后） | 流畅无截断 |
| 百万点回放浏览 | 卡顿/截断 | 60fps（降采样后） |
| 卡尺拖动取值延迟 | <50ms | <16ms（单帧） |
| 信号增删/显隐重布局 | <100ms | <100ms |
| 双卡尺 Δ 计算精度 | ✅ 原始数据插值 | 保持（降采样不影响测量精度） |

---

## 六、实施计划

| 阶段 | 内容 | 优先级 | 预计工时 |
|------|------|--------|---------|
| **G1** | 视口降采样（min/max 抽稀 + 环形缓冲 + 视口缓存） | 🔴 P0 | 3-4 天 |
| **G2** | 多视图游标联动 + 回放位置同步 | 🟡 P1 | 2-3 天 |
| **G3** | 信号数学运算（计算信号 + 表达式引擎评估） | 🟡 P1 | 3-5 天 |
| **G4** | 区域统计（min/max/avg/σ/RMS） | 🟠 P2 | 2 天 |
| **G5** | 数据导出 CSV + 配置持久化 | 🟠 P2 | 2 天 |
| **G6** | 对比模式 + 注释标记 | 🟢 P3 | 3 天 |

依赖：G1 独立先行（其他功能均受益）；G2/G3 可并行；G4/G5/G6 依赖 G1。

---

## 七、与其他模块的协作

| 协作点 | 机制 |
|--------|------|
| Trace → Graphic | 双击 Trace 信号 / 信号树双击 → `addSignal()`（现有信号 `frameAddToGraphic`） |
| DBC 侧边栏 → Graphic | 信号树双击添加（复用 `addSignal` 交互） |
| Player 回放 → Graphic | `onFrame` 实时喂帧；G2 后回放位置线跨视图同步 |
| 插件系统 → Graphic | 插件 `dataOutputs` 声明的计算信号（如 `J1939.EngineTorque`）可注册到全局信号命名空间，Graphic 可订阅绘制（见插件设计 §7.5） |
| Graphic → Data Window | 信号列表交互复用（Data Window 为 CANoe C3 对标项，另行设计） |
