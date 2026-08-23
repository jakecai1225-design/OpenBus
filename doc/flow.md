# openbus 多协议通用 Flow 架构方案

> **状态：设计稿 v1.6（2026-08-21）——M1/M2/M3 已实施（§十三），F1 剩余未实施**
>
> **目标**：将 openbus 从「CAN 单协议分析工具」逐步演进为「多协议通用数据流分析平台」。
> 第一步落地 Flow 侧栏的多协议流处理模式（CAN Flow / EtherCAT Flow / 通用 Flow /
> 第三方协议扩展），并以此为牵引，建立协议无关的统一报文模型、协议适配层与解析器角色。
>
> **v1.6 增补（2026-08-21）**：画布**通道块收编为单 Filter 块**（§8.1 前置落地）——
> 「CAN 通道 1..N」多块合并为一个 **Filter 过滤块**（§3.5 Filter 角色 UI 前置），
> 数据流过滤配置统一至该块（双击 / 右键「配置过滤条件」，规则以摘要行展示在块内）；
> 块交互统一为：**未启用块单击 = 启用、已启用块单击/双击 = 进入配置、右键 = 配置/启停/增删**；
> 空白区右键不再提供「添加 CAN 通道」。
>
> **v1.6 补记（2026-08-23）**：画布「Data 统计」块更名为 **Watcher 观测块**（块 id
> `data`→`watcher`）——对标 Lauterbach/IAR/Keil 调试器 Watch 窗口：变量观测（DBC
> 信号实时列表：当前值/原始值/Min/Max/单位）+ 总线统计（摘要卡片 / 逐 ID 频率周期
> 抖动 / 错误帧分类 Stuff-Form-ACK-Bit0-Bit1-CRC）。观测页为**壳侧单实例标签页**
> （同 I/O Graph 先例，不经模块编排；工具菜单 / Ctrl+Shift+W / 画布块三入口），
> 设计与取舍详见 `doc/Watcher方案.md`。
>
> **v1.5 增补（2026-08-21）**：§七 侧栏交互改为**模板平铺**——Flow / Trace /
> Graphic 三侧栏不再分节嵌套，直接平铺各协议 / 形态**模板入口**一行一个
>（已落地可点击新建，未落地置灰占位）；CollapsibleSection 组件随之移除。
>
> **v1.4 增补（2026-08-21）**：§十三 M 系列三个切片**代码落地**——M1（侧栏折叠分节 +
> 新建下拉 + 身份字段持久化回填）、M2（`core/busmessage.h` + `core/protocol/` 全套接口/
> 注册表/ CAN·DBC 实现 + `test_protocol` 单测）、M3（画布解析器加载走注册表管道）；
> 各节附**落地记录**（实际文件与实现取舍）。
>
> **v1.3 增补（2026-08-21）**：§三 新增**角色管线模型（§3.5）**——一个 Flow 由
> **Source（源）→ Filter（流级过滤）→ Parser（解析）→ Trace/Graphic（视图消费）**
> 四类角色串联组成；新增**流级过滤**角色（FlowSession 过滤链，复用 `FilterEngine`
> 原语泛化到 BusMessage），并明确流级与视图级（Trace FilterEngine 现状）两级过滤边界。
>
> **v1.2 增补（2026-08-21）**：新增 **§十三 最小改动预埋方案（M 系列）**——将 F1 拆出
> UI 预埋（M1）/ 数据层接口预埋（M2）/ 冷路径试点（M3）三个薄切片先行落地，CAN 为首个
> 样板，热路径与既有管理器零改动，UI 先行预留；§十 增加 M 系列先行说明。
>
> **v1.1 增补（2026-08-21）**：画布中「DBC 数据库」块抽象为通用**解析器（Parser）**
> 角色（§六）——解析器与流类型解耦，不同类型的 Flow 所支持的解析器（可加载的协议
> 描述文件）各不相同。v1.0（2026-08-20）：初版。
>
> **定位**：本方案是 README「整体架构设计」中 `IBusSource` / `BusMessage` 多总线愿景
> （CAN/CAN FD/EtherCAT、多总线时间对齐）在当前模块化架构（doc/拆分应用方案.md）下的
> 落地路径，实施遵循「CAN 零回归、增量演进」原则。

---

## 一、背景与目标

### 1.1 背景

openbus 当前是一条 **CAN 单协议流水线**：数据模型（`CanFrame`）、采集设备
（`CanDeviceManager`）、解码（`DbcManager`）、文件格式（canfileio：ASC/BLF/CSV/PCAP/TRC）、
显示（Trace 12 固定列）、录制回放（`Recorder`/`Player`）全部以 CAN/CAN FD 为中心。
README 的总体架构设计从一开始就规划了多总线能力（统一报文结构体 `BusMessage`、
`bus_type` 字段、CAN + EtherCAT 多总线全局时间对齐、soem 组件选型），但实现阶段
尚未兑现。

用户需求：**提高整个 openbus 对不同协议数据的适应能力**。典型场景：

- 同一次测量中同时接入 CAN（动力域）与 EtherCAT（伺服/IO 总线）数据，统一时间轴分析；
- 接入非标协议或自定义字节流（串口、TCP/UDP、原始文件），复用 Trace/Graphic/录制回放；
- 未来按需扩展 LIN / FlexRay / AUTOSAR以太网 等协议，**不重构上层 UI 与业务模块**。

### 1.2 目标

| # | 目标 | 说明 |
|---|------|------|
| G1 | Flow 侧栏多协议流模式 | 侧栏 Flow 面板平铺协议流模板入口：CAN Flow / EtherCAT Flow / 通用 Flow…（已落地可点击新建，未落地置灰占位） |
| G2 | 协议无关统一报文模型 | 全流水线以 `BusMessage` 为唯一数据单元，CAN 为首个内置适配器 |
| G3 | 协议适配层与解析器层可扩展 | 内置 CAN/General/EtherCAT 三个适配器；解析器（Parser）与流类型解耦（DBC/ARXML/J1939/ENI/ESI/字段布局…）；第三方协议与解析器以插件包形式接入 |
| G4 | 业务模块协议无关 | Trace / Graphic / 统计 / 录制回放不感知具体协议，协议差异由适配器消化 |
| G5 | CAN 零回归 | 既有 CAN 功能在演进过程中行为不变，`.sin` 旧录制文件可读 |

### 1.3 设计原则

1. **只增不改**（同 doc/拆分应用方案.md §4.2、doc/驱动系统方案.md §3.2）：
   `IBusinessModule` / `ShellContext` / 适配器接口只追加新动作、新字段，不改既有签名；
   新增虚函数一律带默认实现并放接口末尾。
2. **适配器消化差异**：协议特有的字段语义、列定义、解码、文件格式全部收敛在
   `IProtocolAdapter` 实现内，越过适配器层的代码只看见统一模型。
3. **热路径扁平、冷路径扩展**：分发热路径只走 `BusMessage` 扁平字段
   （禁止每帧构造 `QVariantMap`）；详情/解码等冷路径才使用扩展属性。
4. **数据源无关**（既有约定延续）：上层模块不区分硬件采集、离线文件、仿真源；
   协议流实例对三者统一绑定。
5. **解析器与流类型解耦**：协议描述文件的加载与建模（Parser，§六）独立于协议流
   （Adapter）——流类型声明可接受的解析器集合（acceptedParsers），同一解析器可
   服务多种流、同一流可加载多种解析器（混合解码）。

---

## 二、现状分析

### 2.1 当前数据流水线（实测代码路径）

```
采集源                                  壳分发枢纽                          业务模块
────────                               ──────────                        ────────
CanSimulator（软件模拟）    ─┐
CanDeviceManager            ─┤          MainWindow::onFrameReceived()      openbus_trace   （invoke "onFrame"）
  ├ CanDeviceZLG            │              (mainwindow_frameflow.cpp)      openbus_graphic （invoke "onFrame")
  ├ CanDevicePeak           ├──────────►   测量门控 m_measurementRunning ─► openbus_flow    （invoke "onFrame"）
  ├ CanDeviceCandle/Kvaser  │              帧计数/状态栏                    Recorder.recordFrame()
  └ CanDeviceSlcan          │                                              BusStatistics.onFrame()
Player（文件回放）           ─┘                                              IoGraph / PluginManager
   └ canfileio: ASC/BLF/CSV/PCAP/TRC        解码：DbcManager（仅 DBC）
```

- 测量启停由 Flow 画布（`MeasurementSetupView`）发起，经
  `shellInvoke("measurementToggled"/"moduleToggled"/...)` 回调壳侧编排
  （mainwindow_frameflow.cpp:342-441）。
- 画布拓扑（v1.6 起）：`Real 实时 / File 开关 → Filter 过滤（单块，规则行内嵌展示，
  数据流过滤统一配置入口）→ DBC 数据库 → Trace / Graphic / Watcher 观测 / 录制 Record`
  （measurementsetupview.cpp buildTopology；v1.5 前为「CAN 通道 1..N」多块动态增删；
  原「Data 统计」块已于 v1.6 补记更名「Watcher 观测」）。
- 侧栏 Flow 面板（`MeasurementSetupPanel`，sidebarpanels.cpp）写作本文时只有一个
  "flow" 列表项，点击打开画布页——**没有多协议模板入口**（M1 落地后已改为
  协议流模板平铺，见 §7.2 / §13.2）。

### 2.2 CAN 绑定点盘点（本方案需要解耦的位置）

| # | 绑定点 | 位置 | 现状 |
|---|--------|------|------|
| 1 | `CanFrame` | src/core/canframe.h | 全流水线唯一数据单元，CAN/CAN FD 字段 |
| 2 | 模块入口动作 `"onFrame"(CanFrame)` | src/core/module/imodule.h 约定 | trace/graphic/flow 三模块的帧入口 |
| 3 | `DbcManager` | src/core/dbcmanager.h | 唯一解码器（DBC；arxml importer 已有雏形）——本方案演进为 DBC 解析器兼容壳（§6.4） |
| 4 | `CanDeviceManager` / `ICanDevice` | src/core/candevice*.h | 采集设备层全部为 CAN |
| 5 | canfileio | src/core/canfileio/ | ASC/BLF/CSV/PCAP/TRC 全为 CAN 格式 |
| 6 | `Recorder` / `Player` | src/core/recorder.h、player.h | QDataStream 直写 CanFrame（`.sin`） |
| 7 | Flow 画布 | src/ui/measurementsetupview.h | 通道块标题硬编码「CAN 通道 N」 |
| 8 | `CanTraceModel` | src/models/cantracemodel.h | 12 固定列全 CAN 语义（ID/DLC/BRS/ESI…） |
| 9 | `BusStatistics` | src/core/busstatistics.h | 总线负载/错误统计仅 CAN 语义 |
| 10 | Graphic 信号 | sigMap 约定（imodule.h:133-138） | 信号经 dbcSignalToMap 序列化，隐含 DBC |

### 2.3 已具备的多协议基础（可复用资产）

| 资产 | 位置 | 复用方式 |
|------|------|---------|
| README 多总线愿景（`IBusSource`/`BusMessage`/时间对齐） | README.md 整体架构设计 | 本方案的模型命名与分层直接承接 |
| 数据库面板协议分类树 | sidebarpanels.h DbcPanel（CAN/CANFD、CANopen、EtherCAT、LIN、J1939、AUTOSAR 分类节点） | 文件分类展示已就绪，演进为按 ParserRegistry 动态生成的解析器面板（§6.4） |
| 驱动插件 ABI 模式 | src/core/driver/candriverplugin.h（Q_DECLARE_INTERFACE + IID 版本 + 只增不改） | 协议适配器插件的 ABI 契约直接复用该模式 |
| 模块 DLL + ModuleRegistry + ShellContext | src/core/module/ | 壳与业务模块的协议无关契约已建立，新增 `"onBus"` 动作即可承载新模型 |
| ARXML 导入器 | src/core/dbc/arxml_importer.{h,cpp} | AUTOSAR 报文描述解析的起步代码 |
| 时间戳统一 | CanDeviceManager 已统一 steady_clock 纳秒时间戳（canframe.h:21-24） | 多总线统一时间轴的基础设施已在位 |

---

## 三、总体架构

> **角色管线模型（2026-08-21 增补，详见 §3.5）**：一个 Flow（流会话）由四类角色串联——
> **Source（源）→ Filter（流级过滤）→ Parser（解析）→ Trace/Graphic（视图消费）**，
> 录制 / 统计作为同级消费者挂接在分发之后。

### 3.1 分层视图

```
┌──────────────────────────── UI 交互层（壳 openbus.exe） ─────────────────────────────┐
│  Flow 侧栏           Flow 画布        Trace       Graphic     录制/回放    设置      │
│  （协议流模板平铺）  （分组拓扑）     （多协议列） （适配器解码）                    │
├──────────────────────────── 业务模块层（openbus_*.dll） ────────────────────────────┤
│  openbus_flow · openbus_trace · openbus_graphic · openbus_transceive · …             │
│        统一经 IBusinessModule::invoke("onBus", BusMessage) 接收数据                  │
├──────────────────────────── 核心数据层（openbus_data.dll） ─────────────────────────┤
│  ProtocolRegistry ──► IProtocolAdapter 实例                                          │
│      ├ 内置：CanProtocolAdapter（包装既有 CAN 全家桶）                               │
│      ├ 内置：GeneralProtocolAdapter（通用字节流，F2）                                │
│      ├ 内置/DLL：EthercatProtocolAdapter（F3，openbus_ethercat.dll）                │
│      └ 插件：第三方协议包 .oflow（F4，QPluginLoader 加载）                            │
│  ParserRegistry ──► IBusParser（DBC/ARXML/J1939/ENI/ESI/布局）                       │
│  FlowCore：BusMessage 分发 · FlowSession 流会话管理 · 全局时间轴                     │
│  既有服务：DbcManager · CanDeviceManager · CanSimulator · Player · Recorder          │
├──────────────────────────── 源/文件抽象层 ──────────────────────────────────────────┤
│  硬件采集（驱动插件 .odp） │ 离线文件（busfileio 多协议） │ 仿真源（各协议模拟器）      │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 核心抽象（五个）

| 抽象 | 职责 | 归属 |
|------|------|------|
| `BusMessage` | 协议无关统一报文（统一纳秒时间戳 / bus_type / channel / id / payload / flags） | openbus_data（canframe.h 旁新增 busmessage.h） |
| `IProtocolAdapter` | 协议适配器（流类型）：身份、源能力、通道语义、接受的解析器集合、解码、Trace 列定义、文件格式 | 接口头在 openbus_data；实现分布在 data / ethercat DLL / 插件包 |
| `IBusParser` + `ParserRegistry` | 解析器角色（§六）：协议描述文件 → 统一报文/信号定义；与流类型解耦，按流会话绑定 | openbus_data（接口 + 内置实现；.oflow 插件可自带） |
| `ProtocolRegistry` | 进程内注册表：枚举适配器、按 protocolId 查找；内置注册 + 插件注册 | openbus_data（仿 DbcManager 单例模式） |
| `FlowSession` | 流会话：某协议的一个流处理实例（源绑定 + 通道数 + 流级过滤链 + 解析器绑定 + 使能；角色构成见 §3.5） | openbus_data（结构体）+ openbus_flow（UI 管理） |

### 3.3 数据流（目标态）

```
源（硬件/文件/仿真）
  │  适配器 ingestion：协议原始帧 ──► BusMessage（扁平字段）
  ▼
FlowCore（openbus_data）
  │  全局时间轴标准化 timestampNs ──► 按测量门控 + 流会话使能 + 流会话过滤链（§3.5）分发
  ▼
业务模块 invoke("onBus", BusMessage)
  ├─► Trace：适配器 traceColumns() 渲染列；详情面板走适配器冷路径解码（信号定义来自解析器）
  ├─► Graphic：适配器 decode() 结合解析器统一定义提取 DecodedSignal（DBC/ARXML/J1939 信号 / PDO 映射 / 布局字段）
  ├─► 录制：busfileio 按适配器支持的格式落盘（.sin v2 容器）
  ├─► 统计：按 bus_type 分别计数（每协议一个 BusStatistics 实例）
  └─► 插件宿主：onBus 转发（Python 插件，F4 再开放）
```

### 3.4 与现有模块的边界

- **壳（mainwindow_frameflow.cpp）**：`onFrameReceived(CanFrame)` 改为薄包装——
  CAN 适配器负责 CanFrame→BusMessage 转换后进入 `onBusReceived(BusMessage)`；
  分发目标、测量门控、状态栏逻辑保持原结构。
- **openbus_flow**：画布与流会话 UI（侧栏模板平铺 + 画布分组拓扑）；纯展示与编排，
  不做协议解析。
- **openbus_trace / openbus_graphic / openbus_transceive**：只依赖 BusMessage 与
  适配器提供的展示/解码元数据，不 include 协议私有头。

### 3.5 角色管线模型（Flow 的构成）

一个 Flow（流会话）= 四类角色的有序管线（逻辑视图；物理数据流见 §3.3）：

```
Source 源（设备采集 / 文件回放 / 仿真）
  │  原始帧（CanFrame / General 字节流）
  ▼
Filter 流级过滤（FlowSession 过滤链）
  │  通过帧：BusMessage 扁平属性表达式，热路径零解码
  │  丢弃帧：不进环形缓冲 · 不录制 · 不分发
  ▼
Parser 解析（协议描述定义：DBC / ARXML / J1939 / ENI / 字段布局）
  │  定义经 BusDefinitionStore 供给消费侧；逐帧解码在消费侧冷路径执行（非热路径必经阶段）
  ▼
Trace / Graphic 视图消费（录制 / 统计同级挂接）
```

| 角色 | 职责 | 现状对应 | 目标态落点 |
|------|------|---------|-----------|
| **Source 源** | 产出原始报文：硬件采集 / 离线文件回放 / 仿真 | CanDeviceManager / Player（canfileio）/ CanSimulator | 源/文件抽象层（§3.1 底层）；`FlowSession.sourceBinding` |
| **Filter 流级过滤** | 按 BusMessage 扁平字段筛选进入管线的帧；被滤掉的帧不进 Trace 环形缓冲、不录制、不分发（对标 CANoe Measurement Setup 的 Filter 配置） | 无流级过滤；`FilterEngine`（src/core/filter_engine.h）已是帧级原始属性表达式引擎，仅宿主在 Trace（视图级） | `FlowSession.filterChain`（F1 定义字段 / F2 实施 UI+执行）；画布过滤块（§8.1）；`FilterEngine` 泛化 `evaluate(const BusMessage&)` 后两级宿主复用 |
| **Parser 解析** | 协议描述文件 → 统一定义；逐帧解码由消费侧经适配器冷路径执行 | DbcManager（单协议） | §六 解析器角色：IBusParser / ParserRegistry / BusDefinitionStore |
| **Trace/Graphic 视图** | 消费渲染；录制 / 统计为同级消费者挂接 | Trace / Graphic / Recorder / BusStatistics | `invoke("onBus")` 下游（§3.3） |

**过滤的两级（边界约定）**：

- **流级（本管线中的 Filter 角色）**：位于源与解析器之间、分发枢纽之内执行；只看
  BusMessage 扁平字段（id / channel / flags / payload 长度 / 字节序列），热路径零解码；
  **影响录制与环形缓冲**（滤掉即不落盘，对标 CANoe 测量级过滤语义）。
- **视图级（现状已有，不动）**：Trace 的 FilterEngine + CanTraceProxyModel——只影响
  显示，不影响环形缓冲与录制；可引用解码后的列（Name 等）。Graphic 侧暂无，GV 系列
  按需引入。
- **值级过滤**（谓词引用解析器信号值）天然属于视图级——需要解码结果；流级保持原始
  属性过滤，避免热路径依赖解析器。

**执行原语复用**：`FilterEngine` 现有语法（`id` / `dlc` / `ch` / flag 位 /
`data contains`）即帧级原始属性过滤；泛化签名为 `evaluate(const BusMessage&)`（变量集
由适配器扩展）后，流级（FlowSession 过滤链）与视图级（Trace 代理）两个宿主复用同一
引擎与同一语法。

---

## 四、统一报文模型 BusMessage

### 4.1 结构定义（src/core/busmessage.h，编入 openbus_data）

```cpp
enum class BusType : quint8 {
    Unknown  = 0,
    Can      = 1,   // CAN / CAN FD（CanFrame 全字段无损映射）
    Ethercat = 2,   // EtherCAT 数据帧（过程数据 / 邮箱 / 寄存器访问）
    General  = 3,   // 通用字节流（自定义协议 / 串口 / TCP/UDP / 原始文件）
    // 4-15 预留官方协议；第三方协议插件从 16 起经 ProtocolRegistry 动态分配
};

struct BusMessage
{
    quint64 timestampNs = 0;   // 全局单调纳秒时间戳（多总线统一时间轴）
    BusType bus = BusType::Unknown;
    quint8  channel = 0;       // 协议内通道号（1-based；语义由适配器定义）
    quint8  direction = 0;     // 0=Rx 1=Tx
    quint32 id = 0;            // 协议内标识（CAN ID / EtherCAT 命令字 / 流 ID）
    quint32 flags = 0;         // 位标志；bit 语义由适配器定义（CAN 复用 CanFrame 现有 flag 位）
    QByteArray payload;        // 原始数据（CAN 0-64B / EtherCAT 帧体 / General 任意长）
    // 注意：无 QVariantMap 热路径字段。协议扩展信息一律由适配器按需从扁平字段解码。
};

Q_DECLARE_METATYPE(BusMessage)
```

设计取舍：

- **扁平结构 + QByteArray payload**：跨 DLL 以 QVariant 传输（与 CanFrame 同通道），
  无堆上map开销；拷贝成本与 CanFrame 相当（QByteArray 隐式共享）。
- **flags 复用位约定**：CAN 适配器直接沿用 canframe.h:92-97 的位布局
  （extended/fd/brs/esi/error/direction），保证语义无损迁移。
- **id 语义由适配器解释**：CAN=标识符；EtherCAT=datagram 命令字（LRW/LRD/APRD…）；
  General=用户定义的流编号（无则为 0）。

### 4.2 各协议映射

| 协议 | timestampNs | channel | id | payload | flags（示例） |
|------|-------------|---------|----|---------|--------------|
| CAN/CAN FD | 既有 steady_clock 纳秒 | 物理通道 1..N | CAN ID | 0-64B 数据 | ext/fd/brs/esi/err |
| EtherCAT | 采集纳秒时间戳 | EtherCAT 主站端口/网口 | datagram 命令字 | 帧体（去掉 Ethernet 头） | 邮箱/过程数据/错误/WKC 异常 |
| General | 到达时间 | 流实例编号 | 用户流 ID | 任意长字节 | 起始/结束帧标记、校验错误 |

### 4.3 兼容与序列化

1. **CanFrame 保留**：作为 CAN 适配器内部表示与 `ICanDevice` 层契约不变
   （驱动插件 ABI 不动）；仅在 ingestion/egress 边界做 BusMessage 转换
   （`BusMessage toBusMessage(const CanFrame&)` / `CanFrame toCanFrame(const BusMessage&)`，
   编入 openbus_data）。
2. **模块动作并行**：`invoke("onBus", BusMessage)` 为新约定（imodule.h 注释块追加）；
   F1 期间 trace/graphic/flow 三模块原子切换到 onBus（同一源码树统一构建，无双轨期），
   `"onFrame"` 动作字符串保留不删（只增不改），仅不再被壳调用。
3. **录制容器 v2**：`.sin` 新增带版本头容器——记录块 = `BusType` + BusMessage 字段；
   读取端按版本回退到既有 CanFrame 直读格式（旧文件兼容，G5）。
4. **QVariant 传输**：BusMessage 经 Q_DECLARE_METATYPE 注册后走既有
   `invoke(action, QVariant::fromValue(msg))` 通道，MinGW 跨 DLL 注意事项同 CanFrame
   （DEF-08：字符串槽 + SignalRelay 桥接场景不受影响）。

---

## 五、协议适配层 IProtocolAdapter

### 5.1 接口定义（src/core/protocol/iprotocoladapter.h，编入 openbus_data）

ABI 契约复用驱动插件模式（同 Qt 6.8.x + MinGW-w64 13.1 + C++17；跨边界仅
Qt 值类型/POD；接口只增不改，新增虚函数追加末尾并升 IID）：

```cpp
class IProtocolAdapter
{
public:
    virtual ~IProtocolAdapter() = default;

    // ---- 身份 ----
    virtual QString protocolId() const = 0;    // "can" / "ethercat" / "general" / 第三方
    virtual QString displayName() const = 0;   // "CAN Flow" / "EtherCAT Flow" / "通用 Flow"
    virtual QString iconPath() const = 0;      // :/icons/protocols/can.svg ...

    // ---- 源能力 ----
    virtual QStringList supportedSources() const = 0;   // {"hardware","file","simulator"} 子集
    /// 协议专属源配置页（经 flow 模块以 createPage("source:<protocolId>") 创建）
    virtual QWidget *createSourceConfigPage(const FlowSession &session,
                                            QWidget *parent) = 0;

    // ---- 通道 ----
    virtual int maxChannels() const = 0;       // CAN=16 / EtherCAT=1(网口) / General=8

    // ---- 解析器（协议描述文件；角色定义详见 §六） ----
    virtual QStringList acceptedParsers() const = 0;  // {"dbc","arxml","j1939dbc"} / {"eni","esi"} / {"layout"}

    // ---- 解码（冷路径：详情/Graphic/导出用；信号定义来自流会话已加载的解析器） ----
    struct DecodedSignal { QString name; double value; QString unit; QString raw; };
    virtual QList<DecodedSignal> decode(const BusMessage &msg) const = 0;

    // ---- Trace 展示 ----
    struct TraceColumnDef { QString key; QString title; int width; };
    virtual QList<TraceColumnDef> traceColumns() const = 0;   // 协议专属列
    virtual QString formatField(const BusMessage &msg, const QString &key) const = 0;

    // ---- 文件 IO ----
    virtual QStringList fileFilters() const = 0;   // "*.blf *.asc" / "*.pcapng" / "*.bin *.csv"
};

Q_DECLARE_INTERFACE(IProtocolAdapter, "com.sin.openbus.IProtocolAdapter/1.0")
```

### 5.2 内置 CAN 适配器（CanProtocolAdapter，F1）

- **零重构包装**：源能力 = CanDeviceManager（硬件）+ Player/canfileio（文件）+
  CanSimulator（仿真）；解析器 = 接受 DBC / ARXML / J1939 DBC（DBC 经 DbcManager
  兼容壳提供，ARXML 复用既有 arxml_importer）；
  `traceColumns()` 输出现有 CanTraceModel 12 列定义；
  `decode()` 输出 DBC 信号物理值（复用 findMessage→signalList 解码路径）。
- Flow 画布现有「CAN 通道 N」块全部归属该适配器的流会话，行为不变。

### 5.3 通用 Flow 适配器（GeneralProtocolAdapter，F2）

- **源**：原始字节文件（.bin/.hex）、CSV、串口（QSerialPort）、TCP/UDP（QTcpSocket）。
- **通道**：一个流实例一个通道；`maxChannels()=8`（可同时挂多条流）。
- **解析器**：可选「字段布局」解析器（JSON：偏移/长度/缩放/单位；图形化编辑器
  为 F2 后期增强项），`decode()` 按布局提取字段；不加载解析器时按原始字节流处理。
- **Trace 列**：No./Time/Delta/Ch/Dir/Length/Data（十六进制）+ 用户布局字段列。
- 定位：**接入非标协议的最短路径**——任何能变成字节流的数据都能进 Trace/Graphic/录制。

### 5.4 EtherCAT 适配器（EthercatProtocolAdapter，F3，openbus_ethercat.dll）

- **形态**：独立业务 DLL（经 ProtocolRegistry C 工厂注册，仿 openbus_*.dll 模式），
  同时验证「协议适配器可独立成 DLL」的第三方扩展路径。
- **解析器**：接受 ENI（网络配置：PDO 映射、从站拓扑）+ ESI（设备描述）解析器；
  ENI 解析自研（XML，约 400 行）；侧栏解析器面板 ENI/ESI 节点激活。
- **帧解析**：Ethernet 帧抽出（Ethertype 0x88A4）→ EtherCAT 头 → datagram 遍历
  （命令字/地址/IRQ/WKC）；自研解析器约 600 行，不引 soem（分析器不需要主站栈，
  soem 为 LGPL 需评估，README 已列为备选）。
- **源**：F3 先离线（pcap/pcapng 抓包文件回放，canfileio 的 pcap_reader 泛化）+
  内置 EtherCAT 仿真源（可配置 PDO 周期帧）；实时网口采集（raw socket/Npcap）
  作为 F3 末期或 F4 项。
- **解码**：LRW/LRD 过程映像按 ENI PDO 映射表切信号 → DecodedSignal，
  Graphic 直接绘制（与 DBC 信号同管道）。

### 5.5 第三方协议插件（.oflow 协议包，F4）

- 打包格式对齐驱动包（.odp）/插件包（.opk）：`protocol.json`（清单：id/名称/版本/
  依赖 ABI 版本、自带解析器声明）+ 原生 DLL（实现 IProtocolAdapter，可同时实现
  IBusParser 一并注册，QPluginLoader 加载）。
- 入口：Flow 侧栏底部「＋ 从市场添加协议流」→ 统一插件市场（驱动系统方案 v2
  的统一市场架构，市场条目类型扩一类「协议」）。
- 安全边界：协议插件运行在主进程内（同驱动插件 ABI 约束）；解析崩溃即主程序崩溃，
  市场上架审核 + 崩溃率遥测（复用驱动市场既有机制）。

---

## 六、解析器角色 Parser（协议描述文件通用加载与统一建模）

### 6.1 角色定位

需求截图红框中的「DBC 数据库」块，本方案抽象为通用**解析器（Parser）**角色：
不再与 CAN DBC 绑定，而是独立的「协议描述文件 → 统一报文/信号定义」建模层。

- **解析器与流类型解耦**：解析器只负责把描述文件变成统一定义（报文表 + 信号表），
  不关心数据从哪条流来；流类型（适配器）通过 `acceptedParsers()` 声明自己
  **接受哪些解析器**——即「不同类型的 Flow，所支持的解析器、可加载的协议描述
  文件各不相同」。
- **同一流类型可加载多种解析器**：如 CAN Flow 同时挂 DBC、ARXML、J1939 DBC——
  混合解码（不同描述文件分管不同 ID 区间的报文）。
- **同一解析器可服务多种流**：DBC 家族解析器既服务 CAN Flow，也可服务未来的
  J1939 Flow。
- 画布红框块从「DBC 数据库」泛化为「解析器」块：块内容 = 该流会话已加载的
  解析器文件列表（按解析器类型分组，见 §8.1）。

**流类型 × 解析器接受矩阵**（内置）：

| 流类型 | 接受的解析器（acceptedParsers） | 说明 |
|--------|--------------------------------|------|
| CAN Flow | DBC / ARXML / J1939 DBC | J1939 DBC 为 DBC 方言（29 位 PGN 编码 ID、SPN 信号），初期可并入 DBC 解析器（PDU1/PDU2 专有位处理），按需独立 |
| EtherCAT Flow | ENI / ESI | ENI 为主（PDO 映射驱动帧解码）；ESI 供从站对象字典浏览 |
| 通用 Flow | 字段布局（JSON） | 可选；描述字节流帧内字段偏移/长度/缩放 |
| 第三方流 | 协议包自带解析器声明 | .oflow 清单声明 acceptedParsers，或直接自带新解析器 |

### 6.2 统一定义模型（BusDefinition）

解析器的输出不是各协议私有结构，而是统一「报文/信号定义」模型
（src/core/protocol/busdefinition.h，编入 openbus_data）：

```cpp
struct BusSignalDef {
    QString name;          // 信号名（DBC 信号 / PDO 映射对象 / 布局字段名）
    int startBit = 0;      // 起始位（相对所在报文 / 过程映像区）
    int bitLength = 0;
    bool littleEndian = true;
    double factor = 1.0;   // 缩放
    double offset = 0.0;   // 偏移
    QString unit;          // 单位
    QString comment;       // 注释 / SPN 描述
    // 值表（物理值 ↔ 含义）以 QVariantList 序列化追加（只增不改）
};

struct BusMessageDef {
    quint32 id = 0;        // 协议内报文标识（CAN ID / PDO 过程映像区基址 / 流帧编号）
    QString name;          // 报文名（DBC message / PDO 名 / 布局帧名）
    int length = 0;        // 长度（字节数）
    QList<BusSignalDef> signalList;   // 注：不能用 signals（Qt 宏冲突），沿用 DbcMessage 命名
};

struct BusDefinitionSet {           // 一个描述文件的解析结果
    QString parserId;               // "dbc" / "arxml" / "j1939dbc" / "eni" / "esi" / "layout"
    QString filePath;
    QString protocolHint;           // 目标流类型提示（可空；加载时校验与流类型兼容）
    QList<BusMessageDef> messages;
};
```

各协议描述文件到该模型的映射：

| 描述文件 | message 对应 | signal 对应 | 说明 |
|---------|-------------|------------|------|
| DBC | message（ID/DLC） | signal（startbit/length/字节序/factor/offset/unit） | 与现有 DbcMessage/DbcSignal 一一对应 |
| ARXML | ISignalIPdu | ISignal | 复用既有 arxml_importer 产出结构 |
| J1939 DBC | message（PGN 编码 29 位 ID） | SPN 信号 | PDU1/PDU2 专有位处理 |
| ENI | 过程映像区（PDO 分段） | PDO 映射对象（位偏移） | LRW 帧 payload 按偏移切信号 |
| ESI | 邮箱对象字典条目 | 对象（CoE 索引） | 详情浏览用，不参与帧解码 |
| 字段布局 JSON | 帧布局 | 字段（offset/len/type/scale） | 通用流字段提取 |

### 6.3 解析器接口与注册表

```cpp
class IBusParser
{
public:
    virtual ~IBusParser() = default;

    virtual QString parserId() const = 0;      // "dbc" / "arxml" / "j1939dbc" / "eni" / "esi" / "layout"
    virtual QString displayName() const = 0;   // "DBC 数据库" / "ARXML" / "ENI (EtherCAT)" ...
    virtual QString iconPath() const = 0;
    virtual QStringList fileExtensions() const = 0;   // {"dbc"} / {"arxml"} / {"eni","xml"} ...

    /// 解析描述文件 → 统一定义集（失败返回空集并经 error 上报）
    virtual BusDefinitionSet parse(const QString &filePath, QString *error) const = 0;
};

Q_DECLARE_INTERFACE(IBusParser, "com.sin.openbus.IBusParser/1.0")
```

- **ParserRegistry**（openbus_data，与 ProtocolRegistry 并列）：枚举解析器类型；
  `.oflow` 协议包可同时注册适配器与解析器。
- **BusDefinitionStore**（openbus_data）：持有全部已加载 BusDefinitionSet，提供
  `findMessage(protocolId, id)` / `findSignal(...)` 索引查找——接替 DbcManager 的
  跨文件 O(1) 索引职责（多解析器命名空间按「流会话 + parserId + 文件」隔离，
  避免不同解析器的同名报文互相覆盖）。

### 6.4 与 DbcManager 的演进关系（兼容策略）

- DbcManager **保留**：现有 `loadDbc/unloadDbc/findMessage` API 是壳与各模块的
  既有依赖（mainwindow_frameflow.cpp、dbc 模块详情页等），F1 不动其调用方。
- 演进路径：DbcManager 内部实现改为「DBC 解析器 + BusDefinitionStore」的薄壳
  （findMessage 转发到 store 的 CAN 命名空间查询），对外行为零变化；
  新代码一律走 store / 适配器管道，DbcManager 冻结不再扩能力。
- 侧栏「数据库面板」（DbcPanel）演进为**解析器面板**：分类树节点从静态协议分类
  改为 ParserRegistry 动态生成（DBC / ARXML / J1939 / ENI / ESI / 字段布局…），
  点击文件 → 加载进对应流会话的解析器绑定（多会话时弹出目标选择）。

### 6.5 解析器在数据流中的位置

```
描述文件（.dbc / .arxml / .eni / …）
        │  IBusParser::parse()（加载时，一次性）
        ▼
BusDefinitionSet ──► BusDefinitionStore（消息/信号索引）
                            ▲ 查询（冷路径）
适配器 decode(msg) ────────┘
        ▲ payload / id
BusMessage（热路径分发，不经解析器）
```

- **热路径不经过解析器**：BusMessage 分发与解码无关的链路（录制 / 统计 /
  Trace 原始列）零开销；解析器只在「Graphic 添加信号 / Trace 详情解码 /
  报文名列渲染」等按需查询时介入——与现状 DbcManager::findMessage 的
  使用时机一致（冷路径原则 §1.3-3）。

---

## 七、Flow 侧栏设计（本方案交互重点）

### 7.1 现状与差距

- 现状：`MeasurementSetupPanel`（侧栏 ActivityBar「Flow」图标对应面板）仅一个
  "flow" 列表项 + 提示文案，点击打开画布页。
- 差距：无法表达「多种协议数据流的流处理模式」；无法在不打开画布的情况下
  管理（新建/启停/删除）流实例。

### 7.2 交互设计（目标态，v1.5 修订：模板平铺）

> **v1.5 修订**：v1.4 的「每协议一折叠节 + 节内实例列表」层级过深，按产品决策
> 改为**单层平铺**——侧栏直接展示各协议流**模板入口**，一行一个，不再分节嵌套；
> v1.4 引入的 CollapsibleSection 组件随之移除。
>
> **版式补记（2026-08-23）**：三侧栏（Flow/Trace/Graphic）统一 VS Code Explorer
> 垂直版式——**「已打开」区置顶**（对标 OPEN EDITORS；Flow 由壳 refreshPanelLists
> 按标签页喂入），**新建模板行与操作按钮收拢到面板下方**（Trace 删除 /
> Graphic 添加信号+删除 / Flow 从市场添加协议流）。

```
┌─ Flow（侧栏面板，沿用现有标题） ──────────────────┐
│  ── 已打开 ──                                       │
│    Flow            ← 行点击打开/聚焦画布页（壳喂入） │
│  ── 新建协议流 ──                                   │
│  ＋ CAN Flow        ← 注册表适配器（可点击：新建/打开 CAN 流） │
│  ＋ EtherCAT Flow   ← 置灰占位（F3 落地后由注册表接管启用）   │
│  ＋ CANopen Flow    ← 置灰占位（规划中）                     │
│  ＋ 通用 Flow       ← 置灰占位（F2）                        │
│                                                    │
│  （画布操作提示：未启用块单击启用，已启用块单击/双击进配置，右键配置/启停/增删）│
│  ＋ 从市场添加协议流  ← F4 生态入口（禁用，面板底部）        │
└────────────────────────────────────────────────────┘
```

Trace / Graphic 侧栏同构平铺（形态模板入口，Trace模块设计文档.md §10.7 /
Graphic模块设计文档.md §11.7）：

```
┌─ Trace ──────────────────┐   ┌─ Graphic ───────────────┐
│  ── 已打开 ──             │   │  ── 已打开 ──             │
│  （实例列表：切换/右键/删） │   │  （页面列表：切换/右键/删） │
│  ── 新建 Trace ──         │   │  ── 新建 Graphic ──       │
│  ＋ 帧列表     （可点击）  │   │  ＋ 时序波形   （可点击）  │
│  ＋ 事务配对   （置灰）    │   │  ＋ XY 关联    （置灰）    │
│  ＋ 聚合监视   （置灰）    │   │  ＋ 数字总线   （置灰）    │
│  ＋ 文本日志流 （置灰）    │   │  ＋ 状态时间线 （置灰）    │
│  ＋ 字节流     （置灰）    │   │  ＋ 仪表盘     （置灰）    │
│  ＋ 时序段     （置灰）    │   │  ＋ 柱状统计   （置灰）    │
│  [－删除]   ← 底部操作     │   │ [＋添加信号][－删除] ← 底部 │
└──────────────────────────┘   └──────────────────────────┘
```

交互规则：

| 操作 | 行为 |
|------|------|
| 单击已启用模板行 | 新建该协议流 / 形态实例（F1 FlowSession 落地前，CAN Flow = 打开/聚焦既有画布页） |
| 置灰占位行 | 不可点击；tooltip 标注落地阶段（F2/F3/规划/TR·GV 系列） |
| 协议包安装（F4） | `adapterRegistered` → 模板行重灌：占位行自动转为正式可点击行 |
| 流实例管理 | 经画布页分组框交互（v1.6 统一规则：未启用单击 = 启用、已启用单击/双击 = 配置、右键 = 配置/启停/增删，§8.1）；F1 需要侧栏实例管理时再于「已打开」区扩展 |
| 「从市场添加协议流」 | 打开统一插件市场并筛选「协议」类目（F4） |

### 7.3 模板行生成（注册表驱动）

- Flow 模板行由 `ProtocolRegistry::adapters()` 枚举动态生成（每适配器一行，
  标题 = `displayName()`）；未落地协议（EtherCAT / CANopen / 通用）以置灰
  占位行预埋，同 `protocolId` 适配器注册后自动接管（`adapterRegistered` →
  `rebuildTemplates()` 重灌，DEF-08 字符串信号经 SignalRelay 桥接）。
- Trace / Graphic 模板行当前为静态预埋（六形态一行一个，仅帧列表 / 时序波形
  可点击）；TR1 / GV1 落地 TraceFormRegistry / GraphicFormRegistry 后改为
  注册表枚举生成，UI 结构不再返工。
- ~~折叠栏组件 CollapsibleSection~~（v1.5 移除）：平铺方案下无消费点，
  组件文件与 QSS 样式一并删除；后续若需分组收纳再按需重建。

### 7.4 状态与持久化

- FlowSession 列表序列化进工程状态 JSON（`ProjectContext.stateJson`，键
  `"flows": [{sessionId, protocolId, name, sourceBinding, channelCount,
  filters: [{expr, enabled}], parsers: [{parserId, filePath, enabled}], enabled}]`），随工程保存/切换恢复
  （对齐既有 layoutConfig 机制）。
- 流会话与画布分组、解析器面板、模块实例门控三方联动：
  删除 CAN Flow 会话 = 画布移除该分组下全部通道块 + 该会话解析器绑定解绑
  （描述文件本身不从面板卸载）。

---

## 八、Flow 画布多协议化

### 8.1 拓扑分组演进

现状画布（v1.6 起）：`Real/File → Filter 过滤（单块，原「CAN 通道 1、
CAN 通道 2」多块已收编，规则行内嵌块内）→ DBC 数据库 → Trace/Graphic/Watcher/Record`。
目标态两处泛化：Filter 块所在列引入**协议流分组框**
（首轮截图红框——CAN 通道分组——的产品化，分组框成为实际渲染的容器）；
原「DBC 数据库」块抽象为通用**解析器（Parser）块**（本轮截图红框，角色定义
见 §六）——按流会话挂载该流类型所接受的解析器文件：

```
列1 数据源        列2 协议流分组（按适配器分块）      列3 解析器 Parser     列4 模块
─────────       ─────────────────────────        ──────────────       ─────────
Real 实时  ──►  ┌─ CAN Flow 1 ──────────────┐    ┌ 解析器(CAN) ─┐      Trace1
(File 开关)     │  CAN 通道 1   ON ●        │ ─► │ DBC×2 J1939×1│ ─►   Graphic1
                │  CAN 通道 2   ON ●        │    └──────────────┘  Watcher 观测
                └───────────────────────────┘                        录制 Record
                ┌─ EtherCAT Flow 1 ─────────┐    ┌ 解析器(ECAT)─┐
                │  ECAT 帧流    ON ●        │ ─► │ ENI×1       │
                └───────────────────────────┘    └──────────────┘
                ┌─ 通用 Flow 1 ─────────────┐    （解析器可选：
                │  串口流 COM3  ON ●        │ ─►   字段布局 JSON）
                └───────────────────────────┘
```

- 分组框整体可点击使能（等价该 FlowSession 启停）；框内块沿用 v1.6 统一交互规则：
  未启用块单击 = 启用、已启用块单击/双击 = 进入配置、右键 = 配置/启停/增删
  （空白区右键不再提供「添加 CAN 通道」——通道块已收编为 Filter 块）。
- **解析器块为通用角色**：每个协议流分组带一个解析器块，块内列出该流会话
  已加载的解析器文件（按类型分组，如 `DBC×2 J1939×1`）；右键「加载解析器…」
  的文件对话框过滤项 = 该流类型适配器的 `acceptedParsers()` 声明（CAN Flow
  可选 DBC/ARXML/J1939，EtherCAT Flow 仅 ENI/ESI，通用 Flow 仅字段布局）；
  单个解析器文件可独立启停/卸载；通用 Flow 不加载解析器时连线直连模块列。
- **过滤块（F2，角色定义见 §3.5；v1.6 已前置落地单 Filter 块）**：目标态每个
  协议流分组可挂一个过滤块（列 2 与列 3 之间），承载该 FlowSession 的流级过滤链
  （BusMessage 扁平属性表达式，复用 FilterEngine 语法）；无规则时连线直连
  （与解析器块可选同理）。现状（v1.6）：画布已有一个全局 Filter 过滤块
  （`source → filter → database`），双击/右键「配置过滤条件」打开规则对话框
  （ID 范围 / 帧类型 / 方向逐条添加），规则以摘要行展示在块内并即时生效
  （`filterRulesChanged` 信号，规则链接线留给 F2 FlowSession 落地）。
- 模块块保持全局共享（Trace/Graphic/Watcher/录制不按协议拆分）；
  每个模块块增加「订阅协议」过滤入口（F2：模块实例可选只接收某些协议的数据，
  默认全部——保持现行为）。

### 8.2 BlockItem 扩展（只增字段）

```cpp
struct BlockItem {
    // ... 既有字段不变 ...
    QString protocolId;    // 新增：块所属协议（"" = 全局块：source/module）
    QString sessionId;     // 新增：块所属流会话（channel 分组块 / parser 解析器块）
};
```

- 侧栏 ↔ 画布分组双向联动：`moduleToggled` shellInvoke 参数
  QVariantList 尾部追加 `protocolId`、`sessionId`（只增不改：旧接收端按位置
  解包不受影响）。

---

## 九、业务模块影响分析

| 模块 | 改造点 | 阶段 |
|------|--------|------|
| 壳 frameflow | `onBusReceived(BusMessage)` 分发枢纽；`onFrameReceived(CanFrame)` 保留为 CAN 包装入口；状态栏帧计数按 bus_type 分列 | F1 |
| 数据层 openbus_data | IBusParser/ParserRegistry/BusDefinitionStore 落地（§六）；DbcManager 冻结为 DBC 解析器兼容壳（findMessage 等 API 行为零变化） | F1 |
| openbus_trace | 数据入口切 `"onBus"`；列模型 = 通用基础列（No./Time/Delta/Ch/Dir/Protocol）+ 适配器 traceColumns() 动态列（CAN 下与现 12 列一致）；详情面板经适配器 decode() 冷路径（信号定义来自解析器） | F1（CAN）→F2/F3（列泛化） |
| openbus_graphic | `addSignal` sigMap 追加 `protocolId` 字段（尾部追加）；信号源从 DBC 查找改为「解析器统一定义 + 适配器 decode()」管道（CAN 输出不变） | F1（协议字段）→F3（EtherCAT 信号） |
| openbus_transceive | 录制：busfileio + .sin v2 容器（BusType 前缀）；回放：Player 按版本读新旧格式；发送页保持 CAN（协议发送页 = 适配器 createSourceConfigPage 体系内的能力，F2+） | F1（录制容器）→F2 |
| openbus_flow | 侧栏模板平铺 + FlowSession 管理 + 画布分组拓扑 + 解析器块 + 流配置页 `flowcfg:<sessionId>` | F1（CAN 单行）→F2/F3 |
| BusStatistics | 每协议一实例（QHash<BusType, BusStatistics*>），总线负载分协议显示 | F2 |
| PluginManager | Python 插件 onBus 转发与协议订阅声明（plugin.json `buses: []`） | F4 |
| 数据库面板 DbcPanel | 演进为解析器面板（§6.4）：分类树按 ParserRegistry 动态生成（DBC/ARXML/J1939/ENI/ESI/字段布局）；加载文件即绑定到流会话的解析器 | F1（DBC/ARXML）→F3（ENI/ESI） |

---

## 十、实施阶段划分

> 估时为净开发人日，含单测与冒烟；每阶段结束跑全量 ctest + 真机 CAN 回归
> （对齐 doc/构建基线.md 的验收口径）。

### M 系列 — 最小改动预埋（先行薄切片，约 5-7 人日，详见 §十三）

> 2026-08-21 增补：F1 一步跨度较大，先拆出三个**最小改动薄切片**先行落地——UI 先行
> 预留、接口预埋不接线、冷路径试点；CAN 为首个样板，热路径与既有管理器零改动。
> M 系列完成后 F1 剩余约 3-5 人日。

| 切片 | 内容 | 工时 |
|------|------|------|
| **M1** | UI 预埋与身份标识：Flow/Trace/Graphic 三侧栏模板平铺（CAN Flow / 帧列表 / 时序波形 可用 + 未落地形态置灰占位）+ protocolId/formId 尾部追加与持久化回填（v1.5：折叠分节方案改平铺） | ~2 人日 |
| **M2** | 数据层接口预埋：busmessage.h + IProtocolAdapter/ProtocolRegistry + CanProtocolAdapter 薄实现 + IBusParser/ParserRegistry/BusDefinitionStore + DbcParser 直通（纯新增不接线） | ~2-3 人日 |
| **M3** | 冷路径试点：画布「DBC 数据库」块经「注册表 → 解析器 → DbcManager」管道加载（行为零变化，样板代码路径） | ~1-2 人日 |

### F1 — 地基：统一模型 + CAN 适配器 + 解析器抽象 + 侧栏模板平铺（约 8-10 人日）

| 项 | 内容 |
|----|------|
| 数据层 | busmessage.h + IProtocolAdapter/IBusParser 接口 + ProtocolRegistry/ParserRegistry + BusDefinitionStore + DBC/ARXML 解析器（DbcManager 演进为兼容壳）+ CanProtocolAdapter（包装 CanDeviceManager/canfileio/CanSimulator） |
| 壳 | onBusReceived 分发枢纽 + onFrameReceived 薄包装 + invoke("onBus") 约定 |
| 模块 | trace/graphic/flow 原子切换 onBus（同一构建无兼容窗口）；sigMap 尾部 +protocolId |
| 录制 | .sin v2 容器（写新读新旧双格式） |
| UI | Flow 侧栏模板平铺（CAN Flow 单行 + 占位行）+ 画布 BlockItem 协议分组（CAN）+ 解析器块（DBC/ARXML 加载）+ 数据库面板 → 解析器面板（DBC/ARXML） |
| 验收 | ① CAN 全功能回归（Trace 12 列、Graphic 信号、录制回放、离线分析、Flow 画布）② 旧 .sin 可回放 ③ ctest 7/8 基线不降 ④ 侧栏模板行新建/启停/删除 CAN 流会话闭环 ⑤ 同一 CAN Flow 会话同时加载 DBC 与 ARXML 解析器文件，Trace 报文名 / Graphic 信号混合解码正确 |

### F2 — 通用 Flow（约 4-5 人日）

| 项 | 内容 |
|----|------|
| 适配器 | GeneralProtocolAdapter：原始文件/CSV/串口/TCP-UDP 源、字段布局解析器（JSON 布局文件经 ParserRegistry 注册）、通用 Trace 列 |
| UI | 侧栏「通用 Flow」模板行启用 + 流配置页（源参数/字段布局编辑器基础版）+ 画布通用分组 |
| 统计 | 分协议 BusStatistics + 状态栏分协议帧计数 |
| 验收 | 串口/TCP 字节流 → Trace 显示 → 字段提取 → Graphic 绘制 → 录制回放全链路 |

### F3 — EtherCAT Flow（约 10-14 人日）

| 项 | 内容 |
|----|------|
| DLL | openbus_ethercat.dll：ENI/ESI 解析器（经 ParserRegistry 注册）、datagram 解析器、PDO 信号解码、EtherCAT 仿真源 |
| 源 | pcap/pcapng 离线回放（pcap_reader 泛化到 EtherType 0x88A4）；实时网口采集列为独立后续项 |
| UI | 侧栏 EtherCAT 模板行启用 + 解析器面板 ENI/ESI 节点 + Trace EtherCAT 列/详情 |
| 验收 | 抓包文件 → 过程数据信号 → Graphic 绘制 → 与 CAN 流同窗时间对齐显示 |

### F4 — 协议插件生态（约 5-6 人日）

| 项 | 内容 |
|----|------|
| 打包 | .oflow 协议包格式（适配器 + 可选自带解析器）+ QPluginLoader 加载 + ABI 版本校验 |
| 市场 | 统一插件市场新增「协议」类目；侧栏「＋ 从市场添加协议流」入口 |
| 插件宿主 | Python 插件 onBus 转发与协议订阅 |
| 验收 | 官方示例协议包（如 LIN 或 CANopen 精简版）从市场安装 → 侧栏出现模板行 → 全链路可用 |

---

## 十一、风险与对策

| # | 风险 | 影响 | 对策 |
|---|------|------|------|
| R1 | CAN 回归风险（F1 动到分发枢纽） | 核心功能劣化 | 适配器零重构包装（不重写 CAN 路径）；F1 验收含全量 CAN 回归清单；分发枢纽逻辑保持原顺序 |
| R2 | BusMessage QVariant 跨 DLL 传输（MinGW DEF-08 已知坑） | 信号连接静默失败 | 与 CanFrame 同机制（Q_DECLARE_METATYPE + invoke 字符串动作）；ui_offscreen 冒烟覆盖 |
| R3 | 热路径性能（多协议并发帧率） | Trace 卡顿/丢帧 | 扁平结构无 map 构造；分发仍单线程顺序（与现架构一致）；必要时按 bus_type 分队列 |
| R4 | .sin 格式切换 | 旧工程/旧文件不可读 | v2 容器向后兼容读；canfileio 单测加旧格式用例 |
| R5 | EtherCAT 解析复杂度（ENI 变体、多从站拓扑） | F3 超期 | F3 范围收敛为「单主站 pcap + ENI 静态解析」；邮箱/CoE 解析列为 F3.5 增量 |
| R6 | 协议插件崩溃传导主进程 | 稳定性 | 市场上架审核 + 崩溃率遥测；高价值低频协议可后移 Python 插件宿主（进程隔离）实现 |
| R7 | 接口只增不改被破坏 | 生态 ABI 混乱 | IProtocolAdapter/IBusParser 新增能力一律「末尾虚函数 + 默认实现 + IID 升版本」（驱动插件既有纪律） |
| R8 | 统一定义模型表达力（各协议描述语义差异：J1939 PGN/SPN、ENI 过程映像、ESI 对象字典） | 解析结果失真或解析器堆积特例 | BusDefinition 采用「公共字段 + 尾部追加扩展」演化（只增不改）；无法映射的语义留在解析器内部消化（decode() 自解释），不污染统一模型 |

---

## 十二、与既有文档的关系

| 文档 | 关系 |
|------|------|
| README.md 整体架构设计 | 本方案是其 `IBusSource`/`BusMessage`/多总线时间对齐愿景的实施路径；README 该节在 F1 落地后补「已实现」标注 |
| doc/拆分应用方案.md / 拆分应用实施方案.md | 适配器/解析器/DLL 边界遵循其模块化原则；openbus_ethercat.dll 为新增业务 DLL，注册方式对齐 ModuleRegistry 模式 |
| doc/驱动系统方案.md | ABI 只增不改纪律与统一市场架构复用；.oflow 包对齐 .odp 打包与上架机制 |
| doc/插件系统方案.md | F4 的 Python 插件 onBus 转发与协议订阅声明扩展其宿主协议 |
| doc/需求文档.md | Trace 多协议列与 B16 自定义列协同（适配器列与自定义列同管道；B16-3 的 DBC 信号值引用同步泛化为解析器信号引用）；后续 Flow 相关需求编号从 B18 起接续 |
| doc/Trace模块设计文档.md / Graphic模块设计文档.md | 列模型与信号管道泛化的详细设计在其文档内各自补章节；Trace 视图形态（TraceForm，六种基础形态 + TR 系列路线）见 Trace模块设计文档.md §十，`traceColumns()` 落地时预留形态接口；Graphic 可视化形态（GraphicForm，六种基础形态 + GV 系列路线）见 Graphic模块设计文档.md §十一，`decode()` 输出即各形态统一数据源，`BusSignalDef` 预留 valueType/值表字段 |

## 十三、最小改动预埋方案（M 系列：渐进式落地路径）

> 2026-08-21 增补。§十 F1 是完整的首次落地（约 8-10 人日，含热路径切换与录制容器），
> 一步跨度大、回归面广。本章将其拆出**三个薄切片先行**（M1/M2/M3，合计约 5-7 人日），
> 每个切片独立编译、独立验收、CAN 零回归；**CAN 分析即首个落地样板**——所有通用化机制
> 先以 CAN 跑通、形成标准代码路径，再接新协议（EtherCAT / 通用 Flow / 第三方）。UI 先行
> 预留（模板平铺入口 / 身份字段，v1.5 修订），数据层接口预埋但不接线，热路径与既有管理器
> （DbcManager / CanTraceModel / GraphicView）零改动。

### 13.1 拆薄原则

1. **预埋不改流**：热路径保持 `onFrameReceived(CanFrame)` 原样；BusMessage / 适配器 /
   解析器仅落地接口与 CAN 实现，不改任何既有调用链。
2. **UI 先行预留**：侧栏模板平铺入口（协议 / 形态一行一个，未落地置灰占位）、
   协议 / 形态身份字段先就位——F1 剩余与 TR / GV 系列落地时 UI 不再返工
   （v1.5 修订：原「折叠分节 + 新建下拉」方案改单层平铺）。
3. **每步零回归门禁**：每切片结束跑全量 ctest（7/8 基线）+ 真机 CAN 冒烟；可感知行为
   变化仅限侧栏视觉（模板平铺与置灰占位行）。
4. **样板代码路径**：M3 的「UI → 注册表 → 适配器 / 解析器 → 既有管理器」闭环是后续所有
   协议接入的标准路径（新协议照抄此模式，只换协议侧实现）。

### 13.2 M1 — UI 预埋与身份标识（约 2 人日，纯壳层）

| 项 | 内容 | 涉及 |
|----|------|------|
| Flow 侧栏 | MeasurementSetupPanel 改**模板平铺**：注册表适配器一行一个（CAN Flow 可点击，行为不变）；EtherCAT / CANopen / 通用 Flow 置灰占位；底部预留「从市场添加协议流」入口（置灰，F4 启用） | sidebarpanels.{h,cpp} |
| Trace 侧栏 | **形态模板平铺**：六形态一行一个（帧列表可点击 = 新建 Trace，其余置灰占位）；「已打开」实例列表平铺于下方（切换/右键/删除交互不变） | sidebarpanels.{h,cpp}（详见 Trace模块设计文档.md §10.7） |
| Graphic 侧栏 | 同上：六形态平铺（时序波形可点击，其余置灰）+「已打开」页面列表 | sidebarpanels.{h,cpp}（详见 Graphic模块设计文档.md §11.7） |
| 身份预埋 | TraceTab / GraphicTab / 画布 BlockItem 尾部追加 `protocolId`（默认 "can"）与 `formId`（"framelist" / "waveform"）；工程持久化 JSON 写入新字段，读取缺省回填（旧工程兼容） | trace / graphic / flow 模块 + 工程持久化 |

**验收**：① CAN 全功能回归（ctest 基线 + 真机冒烟）② 三个侧栏平铺呈现模板入口
（CAN Flow / 帧列表 / 时序波形可用，占位行置灰）③ 旧工程文件可打开且身份字段回填正确。

**落地记录（2026-08-21）**：身份字段落地 `ProjectTraceInstance` /
`ProjectGraphicInstance`（DMV 默认值 + JSON 读写 + 旧工程自动回填）、`TraceTab` /
`GraphicView` / `BlockItem` 尾部追加。**v1.5 修订（同日）**：初版按折叠分节方案实现
（CollapsibleSection 组件 + 三侧栏分节 + 「新建」下拉），产品评审后改为**模板平铺**
——CollapsibleSection 组件及 QSS 样式移除，三侧栏改为平铺模板入口行（QListWidget，
已落地行可点击、占位行 `Qt::NoItemFlags` 置灰）；Flow 模板行注册表驱动生成，
`adapterRegistered` / 主题切换经 SignalRelay 重灌（占位行随适配器注册自动让位）。

**后续修补（2026-08-21）**：离线测量结束后 Flow 页「开始」按钮停留在灰化运行态
（Player 回放至文件末尾后无人复位启停按钮）——`onPlayerFinished` 增加复位链：
壳侧在 `m_measurementRunning` 且数据源为离线时，经 `flowInvoke("setMeasurementRunning",
false)` 复位画布启停按钮（`MeasurementSetupView::setRunning` 纯状态设置，不发
measurementToggled，避免二次停止编排），再次点击「开始」即重放；`onMeasurementToggled`
的两处早退路径（用户取消文件选择 / 全部文件解析失败）同样复位按钮状态。

**后续修补（2026-08-21，v1.6）**：画布「CAN 通道 1..N」多块收编为单 **Filter 过滤块**
（§8.1 前置落地）——`buildTopology` 以 `source_real/source_file → filter → database`
连线替代通道列；`addChannelBlock`/`removeChannelBlock`/`showChannelFilterDialog` 及
`channelFilterRequested` 信号全部移除，新增 `filterRulesChanged(QStringList)`；
`showFilterConfigDialog` 规则对话框（ID 范围/帧类型/方向逐条增删，即时生效）统一承载
数据流过滤配置，空白区与块右键不再出现「添加 CAN 通道」；块交互同步统一为
「未启用单击 = 启用、已启用单击/双击 = 进入配置、右键 = 配置/启停/增删」
（`onSceneClicked`/`onSceneDoubleClicked` 重写，单击建立在 Qt 先 click 后 dblclick
的事件序列上，双击分支天然幂等）。

### 13.3 M2 — 数据层接口预埋（约 2-3 人日，纯新增不接线）

| 项 | 内容 |
|----|------|
| 统一报文 | `busmessage.h`：BusMessage 结构（§四）+ CanFrame⇄BusMessage 转换函数（canframe.h 旁纯新增） |
| 适配器 | IProtocolAdapter 接口（§5.1；新增虚函数一律带默认实现并放接口末尾）+ ProtocolRegistry + CanProtocolAdapter 薄实现：`traceColumns()` 输出现有 12 列定义、`decode()` 复用 DbcManager、`acceptedParsers()={"dbc"}` |
| 解析器 | IBusParser + ParserRegistry + BusDefinitionStore + DbcParser 直通实现（内部调 DbcManager，输出 BusDefinitionSet，§6.3） |
| 侧栏接线 | Flow 侧栏从 M1 硬编码改为 `ProtocolRegistry::adapters()` 枚举生成（仅 CAN 注册，视觉零变化；v1.5 修订后为模板平铺行）——注册表首个消费点 |
| 纪律 | 全部纯新增 + 单测；热路径不动；CanTraceModel / GraphicView / DbcManager 零改动；接口一经注册 ABI 冻结（只增不改） |

**验收**：① ctest 基线不降 ② 新增单测覆盖报文转换 / 注册表枚举 / DbcParser 直通
（BusDefinitionSet 与 DbcManager 查询结果一致）③ 程序行为与 M1 完全一致。

**落地记录（2026-08-21）**：`src/core/busmessage.h`（BusType / BusMessage /
BusMessageFlags 位约定与 CanFrame 序列化位一致 / toBusMessage·toCanFrame inline
转换）；`src/core/protocol/` 新增 `busdefinition.h`（BusSignalDef / BusMessageDef /
BusDefinitionSet；**取舍**：§6.2 代码块的 `signals` 成员名与 Qt 宏冲突
（moc 展开为 public），沿用 DbcMessage 命名改为 `signalList`）、`iprotocoladapter.h`（M2 范围接口——createSourceConfigPage 依赖
FlowSession，按「末尾追加 + 默认实现」纪律推迟 F1）、`protocolregistry.{h,cpp}` 与
`parserregistry.{h,cpp}`（openbus_data 单例，内置 CAN/DBC 懒注册）、
`canprotocoladapter.{h,cpp}`（traceColumns 输出 CanTraceModel 12 列；decode 经
`setDbcManager` 注入引用，M2 由单测注入、F1 由壳接线）、`dbcparser.{h,cpp}`（临时
DbcManager 无状态解析直通映射）、`busdefinitionstore.{h,cpp}`（定义集存储 + 协议
命名空间查找，哈希索引结构就位、线性查找先行）。Flow 侧栏改为
`ProtocolRegistry::adapters()` 枚举生成（注册表首个消费点，仅 CAN 注册视觉零变化；
v1.5 修订后为模板平铺行，经 `rebuildTemplates()` 重灌）。
新增 `tests/test_protocol.cpp`（PT-01..06）编入 ctest。

### 13.4 M3 — 冷路径试点接线（约 1-2 人日，画布解析器块）

| 项 | 内容 |
|----|------|
| 加载入口 | 画布「DBC 数据库」块右键「加载解析器…」：文件过滤器由 `acceptedParsers()` 生成（CAN 下仍 *.dbc，行为一致） |
| 加载管道 | 加载动作经 ParserRegistry → DbcParser → DbcManager 直通（外部行为不变，内部走注册表管道）；DbcManager 保持原样，双入口并存 |
| 样板意义 | 验证「UI → 注册表 → 适配器 / 解析器 → 既有管理器」闭环——后续 EtherCAT（ENI/ESI）、通用 Flow（字段布局 JSON）、第三方协议接入照抄此代码路径 |

**验收**：① 经注册表管道加载 DBC 后，Trace 报文名 / Graphic 信号 / 画布块与直连
DbcManager 完全一致 ② 新增端到端单测（加载 → 查询 → 比对）。

**落地记录（2026-08-21）**：`src/ui/flowmodule.cpp` 的 `dbcSelectRequested` 处理器改走
注册表管道——① 文件过滤器由 CAN 适配器 `acceptedParsers()` 聚合解析器扩展名生成
（对话框标题泛化为「导入协议描述文件」，*.dbc 选择行为不变）② 按扩展名路由
`ParserRegistry::findParserForExtension` → `parse()` → 定义集入
`BusDefinitionStore` ③ `DbcManager::loadDbc` 直通保持原样（双入口并存）。
新增端到端单测 `test_protocol.cpp` PT-07（注册表加载 → store 查询 → 与 DbcManager
逐一比对）。**取舍**：卸载路径（dbcRemoveRequested → 壳 → `unloadDbc`）暂不联动
清理 BusDefinitionStore——M3 阶段 store 为无消费方的被动缓存，DbcManager 仍是唯一
事实源；store 的加载/卸载对称同步随 F1 剩余（DbcManager 冻结为兼容壳）结构性解决。

### 13.5 不做清单（明确推迟，保证「最少修改量」）

| 推迟项 | 去向 |
|--------|------|
| 热路径 `invoke("onBus", BusMessage)` 分发切换 | F1 剩余 |
| .sin v2 录制容器（BusType 前缀，写新读新旧） | F1 剩余 |
| DbcManager 冻结为兼容壳（findMessage 转发） | F1 剩余 |
| CanTraceModel 列模型接 `traceColumns()` | F1 剩余（M2 的 traceColumns 仅供注册表输出） |
| TraceFormRegistry / GraphicFormRegistry | TR1 / GV1（M1 下拉先硬编码单项） |
| 流级过滤链（`FlowSession.filterChain` + 画布过滤块 + FilterEngine 泛化） | F1 剩余（FlowSession 字段定义）/ F2（UI + 分发枢纽执行） |
| 画布流会话分组（FlowSession 绑定） | F1 剩余（M1 仅 BlockItem 加协议字段） |
| 数据库面板 → 解析器面板 | F1 剩余 |
| ARXML 解析器接入注册表 | F1 剩余（M2 仅 DBC 直通，验证模式即可） |

### 13.6 与 F1-F4 / TR / GV 的关系

- **M 系列 = F1 的先行薄切片**：M1-M3（约 5-7 人日）完成后，F1 剩余约 3-5 人日
  （热路径切换、.sin v2、DbcManager 兼容壳、模块原子切换、画布会话分组、解析器面板、
  ARXML 接入）。
- **M1 是 TR1 / GV1 的公共前置**：Trace / Graphic 侧栏分节与新建下拉在 M1 就位，TR1 /
  GV1 落地注册表后下拉项自动扩充，UI 不再返工（Trace §10.7 / Graphic §11.7）。
- **风险与对策**：M2 预埋接口若长期不接线易腐化——单测进 CI、接口注册即 ABI 冻结；
  M3 端到端单测保证注册表管道与直连行为一致；侧栏视觉变化集中在 M1 一次性交付，
  避免多次打扰用户。

---

## 附：术语表

| 术语 | 含义 |
|------|------|
| 协议适配器（Protocol Adapter） | 实现 IProtocolAdapter 的协议封装单元（流类型），消化协议差异 |
| 解析器（Parser） | 实现 IBusParser 的协议描述文件加载单元：DBC/ARXML/J1939 DBC/ENI/ESI/字段布局… → 统一报文/信号定义（§六）；与流类型解耦，按流会话绑定 |
| 统一定义模型（BusDefinition） | 解析器输出的协议无关报文/信号定义（BusMessageDef/BusSignalDef），供解码与列渲染共享 |
| 流会话（FlowSession） | 某协议的一个流处理实例：源绑定 + 通道 + 解析器绑定 + 使能态 |
| 流处理模式 | 侧栏平铺的协议流模板入口形态（CAN Flow / EtherCAT Flow / 通用 Flow…） |
| 协议流分组 | Flow 画布中同一流会话的通道块集合（首轮需求截图红框概念的产品化） |
| 解析器块（Parser 块） | 画布中挂载流会话已加载解析器文件的通用块（原「DBC 数据库」块，本轮截图红框的泛化） |
| .oflow | 第三方协议插件包（原生 DLL + protocol.json 清单，可含适配器与解析器） |
