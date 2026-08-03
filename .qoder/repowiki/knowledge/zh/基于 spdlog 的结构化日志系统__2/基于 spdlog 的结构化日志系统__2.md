---
kind: logging_system
name: 基于 spdlog 的结构化日志系统
category: logging_system
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/main.cpp
---

本项目的日志系统基于第三方库 **spdlog**，在 `src/core/logging.{h,cpp}` 中实现统一的初始化、宏封装与双 sink（控制台 + 滚动文件）输出。`src/utils/logging.h` 作为统一入口重新包含核心头文件，供其他模块使用。

### 架构与组件
- **初始化/关闭**：`logging::init()` 在 `main.cpp` 的 `QApplication` 创建后调用，`logging::shutdown()` 在应用退出前调用，确保缓冲刷新与资源释放。
- **默认日志器**：全局单例 `s_logger`，名称为 `"sin"`，通过 `spdlog::set_default_logger` 注册，所有 `SPDLOG_*` 宏直接生效。
- **Sink 策略**：
  - 控制台 sink：`stdout_color_sink_mt`，级别设为 `debug`，彩色输出。
  - 文件 sink：`rotating_file_sink_mt`，单文件最大 5MB，保留 3 个滚动文件，级别设为 `trace`；若指定目录不可写则回退到当前目录 `./logs`，再失败则仅保留控制台输出。
- **日志格式**：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，输出形如 `[2026-07-29 14:30:00.123] [info] message`。
- **刷新策略**：`flush_on(info)`，即 info 及以上级别自动 flush。

### 使用约定
- **模块标签**：通过 `SIN_LOG_DEBUG/INFO/WARN/ERROR(tag, ...)` 宏记录，第一个参数为模块名（如 `"DbcManager"`、`"FilterEngine"`、`"CanSimulator"`），消息体使用 `fmt::format` 风格占位符 `{}`。
- **线程安全**：两个 sink 均为 `_mt` 变体，支持多线程并发写入。
- **Qt 集成**：日志目录通过 `QStandardPaths::AppDataLocation/logs` 确定，路径转换使用 `QString.toStdString()`。

### 实际使用情况
- 核心模块广泛使用 `SIN_LOG_*` 宏：`dbc_adapter.cpp`、`filter_engine.cpp`、`cansimulator.cpp` 等。
- 部分早期代码仍直接使用 `spdlog::info/warn/error`（如 `appconfig.cpp`），未走 `SIN_LOG_*` 宏包装，缺少模块 tag。
- 日志级别策略：控制台 debug 可见，文件 trace 可见，默认 logger 级别 debug，info 及以上自动 flush。

### 关键文件
- `src/core/logging.h` — 日志宏定义与 API 声明
- `src/core/logging.cpp` — spdlog 初始化、sink 配置、默认日志器
- `src/utils/logging.h` — 统一入口重包含
- `src/main.cpp` — 生命周期内 init/shutdown 调用点