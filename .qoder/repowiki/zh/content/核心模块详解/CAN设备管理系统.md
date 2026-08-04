# CAN设备管理系统

<cite>
**本文引用的文件**
- [README.md](file://README.md)
- [CMakeLists.txt](file://CMakeLists.txt)
- [src/main.cpp](file://src/main.cpp)
- [src/core/canframe.h](file://src/core/canframe.h)
- [src/core/candevice.h](file://src/core/candevice.h)
- [src/core/candevicemanager.h](file://src/core/candevicemanager.h)
- [src/core/cansimulator.h](file://src/core/cansimulator.h)
- [src/models/cantracemodel.h](file://src/models/cantracemodel.h)
- [src/ui/traceview.h](file://src/ui/traceview.h)
- [src/core/filter_engine.h](file://src/core/filter_engine.h)
- [src/core/recorder.h](file://src/core/recorder.h)
- [src/core/player.h](file://src/core/player.h)
- [src/ui/graphicview.h](file://src/ui/graphicview.h)
- [src/core/dbcmanager.h](file://src/core/dbcmanager.h)
- [src/utils/message_queue.h](file://src/utils/message_queue.h)
- [src/core/appconfig.h](file://src/core/appconfig.h)
- [src/core/candevice_zlg.h](file://src/core/candevice_zlg.h)
- [src/core/candevice_zlg.cpp](file://src/core/candevice_zlg.cpp)
- [src/core/candevicemanager.cpp](file://src/core/candevicemanager.cpp)
</cite>

## 更新摘要
**已进行的更改**
- 更新了ZLG CAN设备驱动的架构描述，反映新的双句柄方法（设备句柄+通道句柄）
- 增强了DLL加载机制的详细说明
- 更新了SDK函数签名和调用约定的描述
- 改进了配置管理和错误处理机制的说明
- 添加了通道基架构的技术细节

## 目录
1. [简介](#简介)
2. [项目结构](#项目结构)
3. [核心组件](#核心组件)
4. [架构总览](#架构总览)
5. [详细组件分析](#详细组件分析)
6. [依赖关系分析](#依赖关系分析)
7. [性能考量](#性能考量)
8. [故障排查指南](#故障排查指南)
9. [结论](#结论)
10. [附录](#附录)

## 简介
本系统是一款面向汽车电子与总线调试的CAN/CAN FD报文分析工具，提供实时录制、文件回放、DBC信号解析、Trace列表展示、Graphic波形视图等能力。整体采用Qt6 + C++17构建，UI风格参考VS Code，支持无边框窗口、可停靠面板与多标签编辑区。系统通过统一的设备抽象层隔离硬件差异，结合无锁消息队列与发布订阅机制，实现高吞吐、低延迟的数据流处理。

**最新更新**：ZLG CAN设备驱动已完全重构以支持新的SDK API结构，包括增强的DLL加载机制、通道基架构、改进的配置管理和错误处理。

## 项目结构
- 顶层CMake工程负责Qt6查找、子模块集成与安装规则
- src为核心代码：core（数据模型与引擎）、models（表格模型）、ui（界面）、utils（工具）
- resources存放样式与资源，scripts为构建脚本
- third_party包含第三方库源码或头文件

```mermaid
graph TB
A["CMakeLists.txt"] --> B["src/main.cpp"]
B --> C["src/ui/mainwindow.h"]
C --> D["src/ui/traceview.h"]
C --> E["src/ui/graphicview.h"]
C --> F["src/core/candevicemanager.h"]
F --> G["src/core/candevice.h"]
F --> H["src/core/cansimulator.h"]
F --> I["src/core/candevice_zlg.h"]
I --> J["src/core/candevice_zlg.cpp"]
D --> K["src/models/cantracemodel.h"]
D --> L["src/core/filter_engine.h"]
C --> M["src/core/recorder.h"]
C --> N["src/core/player.h"]
C --> O["src/core/dbcmanager.h"]
F --> P["src/utils/message_queue.h"]
```

**图表来源**
- [CMakeLists.txt:1-63](file://CMakeLists.txt#L1-L63)
- [src/main.cpp:1-35](file://src/main.cpp#L1-L35)
- [src/ui/mainwindow.h:1-226](file://src/ui/mainwindow.h#L1-L226)
- [src/ui/traceview.h:1-189](file://src/ui/traceview.h#L1-L189)
- [src/ui/graphicview.h:1-160](file://src/ui/graphicview.h#L1-L160)
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)
- [src/core/candevice.h:1-80](file://src/core/candevice.h#L1-L80)
- [src/core/cansimulator.h:1-69](file://src/core/cansimulator.h#L1-L69)
- [src/core/candevice_zlg.h:1-109](file://src/core/candevice_zlg.h#L1-L109)
- [src/core/candevice_zlg.cpp:1-414](file://src/core/candevice_zlg.cpp#L1-L414)
- [src/models/cantracemodel.h:1-120](file://src/models/cantracemodel.h#L1-L120)
- [src/core/filter_engine.h:1-59](file://src/core/filter_engine.h#L1-L59)
- [src/core/recorder.h:1-48](file://src/core/recorder.h#L1-L48)
- [src/core/player.h:1-74](file://src/core/player.h#L1-L74)
- [src/core/dbcmanager.h:1-73](file://src/core/dbcmanager.h#L1-L73)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)

**章节来源**
- [CMakeLists.txt:1-63](file://CMakeLists.txt#L1-L63)
- [README.md:235-278](file://README.md#L235-L278)

## 核心组件
- CanFrame：统一帧数据结构，涵盖时间戳、ID、扩展/FD标志、DLC、数据、通道、方向等，并提供序列化接口
- ICanDevice：设备抽象接口，定义枚举、打开/关闭、发送/接收、厂商扩展等
- CanDeviceManager：QObject桥接层，统一管理模拟器与真实设备，对外暴露统一信号
- CanSimulator：后台线程生成模拟流量，通过无锁队列批量投递到主线程
- CanDeviceZLG：ZLG致远电子CAN设备后端，支持USBCAN系列设备的动态DLL加载
- CanTraceModel：QAbstractTableModel实现，支持追加、覆盖模式、行标记与着色
- TraceView/TraceTab：Wireshark风格Trace列表、过滤栏、帧信息与信号解码面板
- FilterEngine：自写递归下降表达式过滤器，支持变量、逻辑与比较运算符
- Recorder/Player：录制器与播放器，分别负责写入与按时间戳回放
- GraphicView：基于QCustomPlot的信号波形视图，支持多轴、卡尺测量
- DbcManager：DBC加载与索引，提供O(1)报文查找与信号解码
- FrameQueue：基于moodycamel::ConcurrentQueue的无锁队列封装
- AppConfig：基于nlohmann/json的配置管理

**章节来源**
- [src/core/canframe.h:1-117](file://src/core/canframe.h#L1-L117)
- [src/core/candevice.h:1-80](file://src/core/candevice.h#L1-L80)
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)
- [src/core/cansimulator.h:1-69](file://src/core/cansimulator.h#L1-L69)
- [src/core/candevice_zlg.h:1-109](file://src/core/candevice_zlg.h#L1-L109)
- [src/models/cantracemodel.h:1-120](file://src/models/cantracemodel.h#L1-L120)
- [src/ui/traceview.h:1-189](file://src/ui/traceview.h#L1-L189)
- [src/core/filter_engine.h:1-59](file://src/core/filter_engine.h#L1-L59)
- [src/core/recorder.h:1-48](file://src/core/recorder.h#L1-L48)
- [src/core/player.h:1-74](file://src/core/player.h#L1-L74)
- [src/ui/graphicview.h:1-160](file://src/ui/graphicview.h#L1-L160)
- [src/core/dbcmanager.h:1-73](file://src/core/dbcmanager.h#L1-L73)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)
- [src/core/appconfig.h:1-73](file://src/core/appconfig.h#L1-L73)

## 架构总览
系统采用"中心化发布订阅"思想：设备抽象层（HAL）将在线采集、离线回放、仿真源统一为标准帧流；核心内核层进行时间对齐与分发；业务服务层订阅数据并执行日志、统计、解析等任务；UI交互层仅消费数据，不直接访问底层。

**更新**：ZLG设备驱动现在采用双句柄架构，通过设备句柄和通道句柄分离管理，提供更灵活的通道控制能力。

```mermaid
graph TB
subgraph "设备抽象层(HAL)"
HAL1["ICanDevice(硬件)"]
HAL2["CanDeviceZLG(ZLG设备)"]
HAL3["CanSimulator(仿真)"]
HAL4["Player(回放)"]
end
subgraph "ZLG SDK层"
SDK1["设备句柄(devHandle)"]
SDK2["通道句柄(channelHandle)"]
SDK3["DLL动态加载"]
end
subgraph "核心内核层"
Core["帧分发中心<br/>时间对齐/缓存"]
MQ["FrameQueue(无锁队列)"]
end
subgraph "业务服务层"
S1["Recorder(录制)"]
S2["FilterEngine(过滤)"]
S3["DbcManager(DNC解析)"]
S4["统计/日志"]
end
subgraph "UI交互层"
UI1["TraceView/TraceTab"]
UI2["GraphicView"]
UI3["MainWindow"]
end
HAL1 --> MQ
HAL2 --> SDK1
HAL2 --> SDK2
HAL2 --> SDK3
HAL3 --> MQ
HAL4 --> MQ
MQ --> Core
Core --> S1
Core --> S2
Core --> S3
Core --> S4
S1 --> UI1
S2 --> UI1
S3 --> UI2
S4 --> UI1
UI1 --> UI3
UI2 --> UI3
```

**图表来源**
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)
- [src/core/cansimulator.h:1-69](file://src/core/cansimulator.h#L1-L69)
- [src/core/player.h:1-74](file://src/core/player.h#L1-L74)
- [src/core/candevice_zlg.h:1-109](file://src/core/candevice_zlg.h#L1-L109)
- [src/core/candevice_zlg.cpp:1-414](file://src/core/candevice_zlg.cpp#L1-L414)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)
- [src/core/recorder.h:1-48](file://src/core/recorder.h#L1-L48)
- [src/core/filter_engine.h:1-59](file://src/core/filter_engine.h#L1-L59)
- [src/core/dbcmanager.h:1-73](file://src/core/dbcmanager.h#L1-L73)
- [src/ui/traceview.h:1-189](file://src/ui/traceview.h#L1-L189)
- [src/ui/graphicview.h:1-160](file://src/ui/graphicview.h#L1-L160)
- [src/ui/mainwindow.h:1-226](file://src/ui/mainwindow.h#L1-L226)

## 详细组件分析

### 设备抽象与设备管理器
- ICanDevice定义统一设备契约，支持枚举、打开/关闭、发送/接收、厂商扩展
- CanDeviceManager作为QObject桥接层，持有ICanDevice或CanSimulator实例，对外发射frameGenerated信号，屏蔽后端差异
- 接收线程通过FrameQueue批量入队，主线程定时drainQueue消费，避免阻塞UI

**更新**：CanDeviceManager现在支持多种设备类型（模拟器、ZLG等），并通过统一的接口管理不同的硬件后端。

```mermaid
classDiagram
class ICanDevice {
+enumerate() DeviceInfo[]
+open(devIndex, channel, arbBaud, dataBaud, canFd) bool
+close() void
+send(frame) int
+recv(timeoutMs, outFrames) int
+pendingCount() int
+isOpen() bool
+deviceName() QString
+vendorCtrl(cmd, param) bool
}
class CanDeviceManager {
-m_kind : DeviceKind
-m_device : ICanDevice*
-m_simulator : CanSimulator*
-m_queue : FrameQueue
+configure(kind, devIndex, channel, arbBaud, dataBaud, canFd) void
+start() void
+stop() void
+sendFrame(frame) bool
+drainQueue() void
<<signals>> frameGenerated(frame)
}
class CanDeviceZLG {
-m_devHandle : void*
-m_channelHandle : void*
-m_dll : QLibrary
+open(devIndex, channel, arbBaud, dataBaud, canFd) bool
+close() void
+send(frame) int
+recv(timeoutMs, outFrames) int
+isAvailable() bool
+enumerate() DeviceInfo[]
}
class CanSimulator {
+start() void
+stop() void
+setIntervalMs(ms) void
<<signals>> frameGenerated(frame)
}
class FrameQueue {
+enqueue(frame) void
+enqueueBatch(frames) void
+tryDequeue(out) bool
+tryDequeueBulk(out, maxCount) size_t
+approxSize() size_t
}
CanDeviceManager --> ICanDevice : "使用"
CanDeviceManager --> CanSimulator : "使用"
CanDeviceManager --> FrameQueue : "无锁队列"
CanDeviceZLG --> ICanDevice : "实现"
```

**图表来源**
- [src/core/candevice.h:1-80](file://src/core/candevice.h#L1-L80)
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)
- [src/core/candevicemanager.cpp:1-204](file://src/core/candevicemanager.cpp#L1-L204)
- [src/core/candevice_zlg.h:1-109](file://src/core/candevice_zlg.h#L1-L109)
- [src/core/cansimulator.h:1-69](file://src/core/cansimulator.h#L1-L69)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)

**章节来源**
- [src/core/candevice.h:1-80](file://src/core/candevice.h#L1-L80)
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)
- [src/core/candevicemanager.cpp:1-204](file://src/core/candevicemanager.cpp#L1-L204)
- [src/core/cansimulator.h:1-69](file://src/core/cansimulator.h#L1-L69)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)

### ZLG设备驱动架构
**新增**：ZLG CAN设备驱动采用全新的双句柄架构，通过设备句柄和通道句柄分离管理，提供更好的灵活性和错误处理能力。

#### 双句柄管理机制
- **设备句柄（devHandle）**：由`ZCAN_OpenDevice`返回，用于设备级操作如波特率设置、设备信息查询
- **通道句柄（channelHandle）**：由`ZCAN_InitCAN`返回，用于通道级操作如数据收发、通道控制
- **DLL动态加载**：运行时加载zlgcan.dll，支持热插拔和设备检测

#### SDK函数映射
```mermaid
flowchart TD
OpenDev["ZCAN_OpenDevice<br/>返回设备句柄"] --> SetBaud["ZCAN_SetValue<br/>设置波特率"]
SetBaud --> InitCh["ZCAN_InitCAN<br/>返回通道句柄"]
InitCh --> StartCh["ZCAN_StartCAN<br/>启动通道"]
StartCh --> SendRecv["ZCAN_Transmit/ZCAN_Receive<br/>数据收发"]
SendRecv --> ResetCh["ZCAN_ResetCAN<br/>复位通道"]
ResetCh --> CloseDev["ZCAN_CloseDevice<br/>关闭设备"]
```

**图表来源**
- [src/core/candevice_zlg.cpp:148-218](file://src/core/candevice_zlg.cpp#L148-L218)
- [src/core/candevice_zlg.cpp:220-236](file://src/core/candevice_zlg.cpp#L220-L236)

#### DLL加载机制
- **智能路径搜索**：应用目录 → ZCANPRO安装目录 → 系统PATH
- **符号解析**：动态解析ZLG SDK函数指针，确保API兼容性
- **错误处理**：完整的错误日志和降级机制

**章节来源**
- [src/core/candevice_zlg.h:1-109](file://src/core/candevice_zlg.h#L1-L109)
- [src/core/candevice_zlg.cpp:1-414](file://src/core/candevice_zlg.cpp#L1-L414)

### Trace追踪与过滤
- CanTraceModel维护帧序列，支持appendFrame/appendFrames、覆盖模式、行标记与自定义颜色
- TraceView/TraceTab提供列排序、漏斗筛选、表达式过滤、右键快速筛选、书签等功能
- FilterEngine编译表达式并对每帧求值，语法包括id/dlc/ch/time/fd/ext/rx/tx/std等变量与逻辑/比较运算符

```mermaid
sequenceDiagram
participant Main as "MainWindow"
participant DevMgr as "CanDeviceManager"
participant Model as "CanTraceModel"
participant View as "TraceView"
participant Filter as "FilterEngine"
Main->>DevMgr : start()
DevMgr-->>Main : frameGenerated(frame)
Main->>Model : appendFrame(frame)
Model-->>View : modelUpdated()
View->>Filter : evaluate(frame)
alt 通过过滤
View->>View : 显示行/更新计数
else 未通过
View->>View : 隐藏行
end
```

**图表来源**
- [src/models/cantracemodel.h:1-120](file://src/models/cantracemodel.h#L1-L120)
- [src/ui/traceview.h:1-189](file://src/ui/traceview.h#L1-L189)
- [src/core/filter_engine.h:1-59](file://src/core/filter_engine.h#L1-L59)
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)

**章节来源**
- [src/models/cantracemodel.h:1-120](file://src/models/cantracemodel.h#L1-L120)
- [src/ui/traceview.h:1-189](file://src/ui/traceview.h#L1-L189)
- [src/core/filter_engine.h:1-59](file://src/core/filter_engine.h#L1-L59)

### 录制与回放
- Recorder根据文件扩展名选择写入器，将CanFrame流式写入BLF/ASC/CSV
- Player从文件或帧序列加载，按原始时间戳播放，支持速度控制、暂停、跳转

```mermaid
flowchart TD
Start(["开始录制"]) --> CheckFile{"检查文件路径"}
CheckFile --> |有效| OpenWriter["打开CanFileWriter"]
CheckFile --> |无效| Error["返回错误"]
OpenWriter --> Loop["循环接收帧"]
Loop --> Write["recordFrame(frame)"]
Write --> UpdateCount["累计帧数"]
UpdateCount --> Loop
Loop --> Stop{"停止录制?"}
Stop --> |否| Loop
Stop --> |是| CloseWriter["关闭写入器"]
CloseWriter --> End(["结束"])
```

**图表来源**
- [src/core/recorder.h:1-48](file://src/core/recorder.h#L1-L48)
- [src/core/player.h:1-74](file://src/core/player.h#L1-L74)

**章节来源**
- [src/core/recorder.h:1-48](file://src/core/recorder.h#L1-L48)
- [src/core/player.h:1-74](file://src/core/player.h#L1-L74)

### 图形视图与DBC解析
- GraphicView基于QCustomPlot绘制多信号曲线，支持单/双卡尺、时间窗口缩放、独立Y轴
- DbcManager加载DBC文件，建立ID→Message哈希索引，提供decodeFrame/decodeSignal解码

```mermaid
classDiagram
class GraphicView {
+addSignal(sig) void
+removeSignal(index) void
+clearSignals() void
+onFrame(frame) void
+loadFile(path) void
-extractValue(frame, sig) double
-extractRaw(frame, sig) quint64
}
class DbcManager {
+loadDbc(filePath) bool
+unloadDbc(filePath) void
+findMessage(id) const DbcMessage*
+findSignal(id, name) const DbcSignal*
+decodeFrame(id, data) QVector<DecodedSignal>
+decodeSignal(id, name, data, outValue) bool
}
GraphicView --> DbcManager : "信号解码"
```

**图表来源**
- [src/ui/graphicview.h:1-160](file://src/ui/graphicview.h#L1-L160)
- [src/core/dbcmanager.h:1-73](file://src/core/dbcmanager.h#L1-L73)

**章节来源**
- [src/ui/graphicview.h:1-160](file://src/ui/graphicview.h#L1-L160)
- [src/core/dbcmanager.h:1-73](file://src/core/dbcmanager.h#L1-L73)

### 应用入口与配置
- main.cpp注册元类型、初始化日志、加载配置、应用主题、创建主窗口
- AppConfig基于nlohmann/json管理settings.json，提供typed getter/setter与持久化

```mermaid
sequenceDiagram
participant App as "QApplication"
participant Main as "main.cpp"
participant Log as "logging : : init()"
participant Cfg as "AppConfig"
participant Theme as "ThemeManager"
participant Win as "MainWindow"
App->>Main : 启动
Main->>Log : init()
Main->>Cfg : load()
Main->>Theme : applyTheme("Light")
Main->>Win : new MainWindow()
Win->>Win : show()
App->>App : exec()
```

**图表来源**
- [src/main.cpp:1-35](file://src/main.cpp#L1-L35)
- [src/core/appconfig.h:1-73](file://src/core/appconfig.h#L1-L73)

**章节来源**
- [src/main.cpp:1-35](file://src/main.cpp#L1-L35)
- [src/core/appconfig.h:1-73](file://src/core/appconfig.h#L1-L73)

## 依赖关系分析
- Qt6 Widgets/PrintSupport为UI基础
- nlohmann/json用于配置管理
- moodycamel::ConcurrentQueue用于无锁队列
- QCustomPlot用于波形绘图
- dbcppp用于DBC解析（在README中提及）
- zlgcan.dll用于ZLG设备驱动（运行时动态加载）

**更新**：新增了ZLG SDK的动态依赖关系。

```mermaid
graph LR
Qt["Qt6 Widgets/PrintSupport"] --> UI["UI层"]
JSON["nlohmann/json"] --> CFG["AppConfig"]
MQ["moodycamel::ConcurrentQueue"] --> Q["FrameQueue"]
QCP["QCustomPlot"] --> GV["GraphicView"]
DBCPP["dbcppp"] --> DM["DbcManager"]
ZLG["zlgcan.dll"] --> ZLGDRV["CanDeviceZLG"]
```

**图表来源**
- [CMakeLists.txt:1-63](file://CMakeLists.txt#L1-L63)
- [src/core/appconfig.h:1-73](file://src/core/appconfig.h#L1-L73)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)
- [src/ui/graphicview.h:1-160](file://src/ui/graphicview.h#L1-L160)
- [src/core/dbcmanager.h:1-73](file://src/core/dbcmanager.h#L1-L73)
- [src/core/candevice_zlg.h:1-109](file://src/core/candevice_zlg.h#L1-L109)
- [README.md:377-384](file://README.md#L377-L384)

**章节来源**
- [CMakeLists.txt:1-63](file://CMakeLists.txt#L1-L63)
- [README.md:377-384](file://README.md#L377-L384)

## 性能考量
- 无锁队列：生产者-消费者模型减少线程竞争，适合高频报文场景
- 批量操作：FrameQueue提供批量入队/出队，降低交互开销
- 覆盖模式：同ID只保留最新帧，降低内存占用，提升滚动性能
- 虚拟模型：Trace使用QAbstractTableModel，避免QTableWidget的性能瓶颈
- 视口采样：GraphicView建议对大数据点进行降采样，保证流畅渲染
- **优化**：ZLG驱动采用双句柄架构，减少SDK调用开销，提高数据传输效率

[本节为通用指导，无需特定文件引用]

## 故障排查指南
- 设备连接失败：检查ICanDevice::open参数与设备序号；确认驱动与权限
- **新增**：ZLG DLL加载失败：检查zlgcan.dll是否存在于应用目录或ZCANPRO安装目录；确认函数符号解析成功
- **新增**：通道初始化失败：验证设备句柄有效性；检查波特率配置是否正确
- 帧丢失：监控FrameQueue.approxSize与pendingFrames，确保主线程及时drain
- 过滤表达式错误：查看FilterEngine.errorString，修正语法
- 录制失败：确认文件路径与写入器初始化；检查磁盘空间与权限
- 回放卡顿：调整Player.speed与UI刷新频率；启用覆盖模式减少数据量

**章节来源**
- [src/core/candevicemanager.h:1-120](file://src/core/candevicemanager.h#L1-L120)
- [src/utils/message_queue.h:1-87](file://src/utils/message_queue.h#L1-L87)
- [src/core/filter_engine.h:1-59](file://src/core/filter_engine.h#L1-L59)
- [src/core/recorder.h:1-48](file://src/core/recorder.h#L1-L48)
- [src/core/player.h:1-74](file://src/core/player.h#L1-L74)
- [src/core/candevice_zlg.cpp:93-127](file://src/core/candevice_zlg.cpp#L93-L127)

## 结论
本系统以清晰的层次化架构与松耦合设计，实现了CAN/CAN FD报文的采集、录制、回放与可视化分析。通过设备抽象、无锁队列与表达式过滤，兼顾了易用性与高性能。**最新更新**：ZLG设备驱动的双句柄架构重构显著提升了设备管理的灵活性和稳定性，为未来扩展更多硬件后端奠定了坚实基础。后续可扩展更多硬件后端、高级统计与脚本能力，满足复杂工程需求。

[本节为总结性内容，无需特定文件引用]

## 附录
- 构建与运行：参考README中的安装教程与CMake配置
- 协议与扩展：预留脚本引擎与AI侧边栏接口，便于未来增强
- **新增**：ZLG设备支持：USBCAN-1/2、USBCANFD-200U/100U、PCI-CANal等设备类型

[本节为补充信息，无需特定文件引用]