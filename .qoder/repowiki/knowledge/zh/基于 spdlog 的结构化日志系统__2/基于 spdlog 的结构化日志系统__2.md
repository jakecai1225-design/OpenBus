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

本项目采用 spdlog 作为底层日志框架，通过 `src/core/logging.h` 和 `src/core/logging.cpp` 提供统一的初始化、宏封装与双 sink 输出（控制台彩色输出 + 滚动文件）。`src/utils/logging.h` 仅作为统一入口重新包含核心头文件，供其他模块使用。

**架构与初始化**
- 应用启动时在 `main.cpp` 中调用 `logging::init()`，默认日志目录为 `QStandardPaths::AppDataLocation/logs`，若不可写则回退到 `./logs`。
- 关闭时调用 `logging::shutdown()` 刷新缓冲并释放资源。
- 可通过 `logging::logger()` 获取原始 `spdlog::logger*` 以进行高级配置。

**Sink 策略**
- 控制台 sink：`stdout_color_sink_mt`，级别设为 debug。
- 文件 sink：`rotating_file_sink_mt`，单文件 5MB，保留 3 个滚动文件，级别设为 trace（记录更详细）。
- 默认 logger 级别为 debug，info 及以上自动 flush。

**日志格式与宏**
- 格式模式：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，输出形如 `[2026-07-29 14:30:00.123] [info] [DbcManager] message`。
- 提供 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 四个宏，第一个参数为 tag（模块名），后续参数通过 `fmt::format` 格式化。
- 当前代码库中使用 tag 包括 `CanSimulator`、`DBC`、`FilterEngine`、`DbcDetailTab` 等。

**使用约定**
- 所有模块通过 `#include "utils/logging.h"` 引入日志能力。
- 日志 tag 应使用模块/类名作为标识，便于按来源过滤。
- 结构化字段通过 `fmt::format` 的 `{}` 占位符传递，避免字符串拼接。