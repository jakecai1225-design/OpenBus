---
kind: logging_system
name: 基于 spdlog 的 SIN_LOG_* 结构化日志系统
category: logging_system
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/main.cpp
---

## 1. 使用的框架与工具

- **底层库**：`spdlog`（位于 `third_party/spdlog/`），通过 CMake 集成。
- **输出格式化工具**：`fmt::format`，用于在宏中格式化消息体。
- **Qt 集成**：使用 `QStandardPaths::AppDataLocation` 定位用户可写目录，`QString` 路径经 `.toStdString()` 传递给 spdlog。

## 2. 核心文件与位置

| 文件 | 作用 |
|---|---|
| `src/core/logging.h` | 定义 `logging` 命名空间、`init/shutdown/logger()` API 以及 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 四个宏 |
| `src/core/logging.cpp` | 实现双 sink 初始化（控制台彩色 + 滚动文件）、默认 logger 创建、模式设置 |
| `src/utils/logging.h` | 薄包装，重新 include `core/logging.h`，作为统一入口 |
| `src/main.cpp` | 调用 `logging::init()` 启动、`logging::shutdown()` 关闭 |

## 3. 架构与约定

### 初始化流程
- `logging::init(logDir)` 在应用启动时调用。若未指定目录，则写入 `<AppData>/logs/sin.log`；不可写时回退到当前工作目录下的 `./logs/sin.log`。
- 创建两个 sink：
  - `stdout_color_sink_mt`：控制台彩色输出，level 设为 `debug`。
  - `rotating_file_sink_mt`：滚动文件，单文件上限 5 MB，保留 3 个历史文件，level 设为 `trace`（比控制台更详细）。
- 默认 logger 名为 `sin`，全局级别 `debug`，按 `info` 及以上自动 flush，pattern 为 `[时间] [级别] 消息`。
- 异常处理：文件 sink 创建失败时捕获 `spdlog_ex`，降级为仅控制台输出。

### 调用约定
- 所有模块通过 `SIN_LOG_<LEVEL>(tag, fmt_args...)` 宏记录日志，第一个参数是**模块标签**（如 `CanDeviceKvaser`、`CanDevicePEAK`、`CanDeviceZLG`），第二个参数开始是 `fmt::format` 风格的占位符。
- 日志格式示例：`[2026-07-29 14:30:00.123] [info] [DbcManager] 解析文件: xxx.dbc`
- 日志器可通过 `logging::logger()` 获取原始 `spdlog::logger*` 指针以进行高级配置。

### 生命周期管理
- 应用入口 `main.cpp` 在 Qt 事件循环前调用 `logging::init()`，退出前调用 `logging::shutdown()` 刷新缓冲并释放资源。

## 4. 约定与约束

- **禁止直接使用 `qDebug()` / `std::cout`**：项目注释明确说明“提供控制台 + 滚动文件双 sink”，并通过 `SIN_LOG_*` 系列宏替代 `qDebug`，所有业务代码均通过该宏输出。
- **模块标识强制**：每个日志调用必须传入 tag 字符串，用于区分来源模块（observed tags 包括 `ICanDevice`、`CanDeviceKvaser`、`CanDevicePEAK`、`CanDeviceZLG` 等）。
- **级别策略**：控制台最低 `debug`，文件最低 `trace`；info 及以上自动 flush，保证关键信息不丢失。
- **日志轮转策略**：固定 5 MB × 3 文件的滚动策略，避免磁盘占用无限增长。
- **容错设计**：当用户数据目录不可写时自动回退到当前目录，再失败则仅保留控制台输出，确保应用不因日志问题崩溃。
- **线程模型**：两个 sink 均为 `_mt`（多线程安全）版本，适合多线程 CAN 设备读取场景。

## 5. 使用范围

目前已在 `src/core/candevice*.cpp`（Kvaser、PEAK、ZLG 驱动加载与操作）中广泛使用，覆盖设备发现、DLL 加载、通道打开/关闭、报文收发等关键路径，形成统一的诊断输出。