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

本项目的日志系统基于第三方库 **spdlog** 实现，提供控制台彩色输出与滚动文件双 sink 的架构，通过统一的 `SIN_LOG_*` 宏在业务代码中调用。

### 1. 使用的框架与工具
- **spdlog**：高性能 C++ 日志库，作为底层实现。
- **fmt**：用于日志消息格式化（通过 `fmt::format`）。
- **Qt6**：用于日志目录定位（`QStandardPaths::AppDataLocation`）和路径管理。

### 2. 核心文件与包
- `src/core/logging.h` / `src/core/logging.cpp`：日志系统初始化、全局 logger 管理、`SIN_LOG_*` 宏定义。
- `src/utils/logging.h`：统一入口头文件，重新包含 `core/logging.h`。
- `src/main.cpp`：应用启动时调用 `logging::init()`，退出时调用 `logging::shutdown()`。
- `third_party/spdlog/`：spdlog 第三方源码。

### 3. 架构与设计决策
- **单例全局 logger**：`s_logger` 为静态 `std::shared_ptr<spdlog::logger>`，通过 `logging::logger()` 获取。
- **双 sink 策略**：
  - 控制台 sink：`stdout_color_sink_mt`，级别 debug。
  - 文件 sink：`rotating_file_sink_mt`，5MB × 3 个滚动文件，级别 trace。
- **日志格式**：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，输出形如 `[2026-07-29 14:30:00.123] [info] message`。
- **默认级别**：全局 logger 级别设为 debug，flush 策略为 info 及以上自动刷新。
- **容错机制**：若指定日志目录不可写，回退到当前目录 `./logs`；若仍失败则仅使用控制台输出。

### 4. 约定与约束
- **模块标签**：所有日志调用必须通过 `SIN_LOG_DEBUG/INFO/WARN/ERROR(tag, ...)` 宏，第一个参数为模块名 tag（如 "DbcManager"、"FilterEngine"），便于按模块过滤。
- **初始化时机**：必须在 `QApplication` 设置应用名称之后调用 `logging::init()`，确保日志目录正确。
- **关闭顺序**：应用退出前必须调用 `logging::shutdown()` 以刷新缓冲并释放资源。
- **日志级别选择**：debug 用于调试信息，info 用于关键流程节点，warn 用于可恢复异常，error 用于错误。
- **线程安全**：sink 使用 `_mt` 后缀版本，支持多线程并发写入。
- **结构化字段**：通过 fmt 风格 `{}` 占位符传递参数，避免字符串拼接开销。

### 5. 使用示例
```cpp
#include "utils/logging.h"

SIN_LOG_INFO("DbcManager", "解析文件: {}", fileName.toStdString());
SIN_LOG_WARN("FilterEngine", "编译失败: '{}' -> '{}', error: {}",
             input, output, errorMsg);
```

### 6. 当前使用情况
项目中已有约 8 处日志调用，分布在 `cansimulator.cpp`、`dbc_adapter.cpp`、`filter_engine.cpp`、`dbcdetailtab.cpp` 等核心模块，覆盖了解析、过滤、回放等关键路径。