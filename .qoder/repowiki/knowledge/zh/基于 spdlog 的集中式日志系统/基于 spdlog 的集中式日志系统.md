---
kind: logging_system
name: 基于 spdlog 的集中式日志系统
category: logging_system
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/main.cpp
---

本项目的日志系统基于第三方库 **spdlog** 实现，采用单例全局 logger + 双 sink（控制台彩色输出 + 滚动文件）架构，通过 `SIN_LOG_*` 宏在业务代码中统一调用。

### 1. 使用的框架与工具
- **spdlog**：作为底层日志引擎，提供高性能异步/同步日志能力。
- **fmt**：用于日志消息格式化（`fmt::format`），配合 `SIN_LOG_*` 宏使用。
- **Qt6**：用于获取可写路径（`QStandardPaths::AppDataLocation`）、创建目录等。

### 2. 核心文件与位置
- `src/core/logging.h` / `src/core/logging.cpp`：日志系统初始化、关闭、默认 logger 获取，以及 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 四个级别宏定义。
- `src/utils/logging.h`：对 `core/logging.h` 的重新包含，作为 utils 层的统一入口。
- `src/main.cpp`：应用启动时调用 `logging::init()`，退出前调用 `logging::shutdown()`。

### 3. 架构与设计决策
- **单例全局 logger**：`s_logger` 为静态 `std::shared_ptr<spdlog::logger>`，通过 `logging::logger()` 暴露指针，`spdlog::set_default_logger` 设置后所有模块可直接使用 `SPDLOG_*` 宏。
- **双 sink 策略**：
  - 控制台 sink：`stdout_color_sink_mt`，线程安全，级别设为 debug。
  - 文件 sink：`rotating_file_sink_mt`，单文件最大 5MB，保留 3 个轮转文件；若指定目录不可写则回退到当前目录 `./logs`。
- **日志格式**：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，输出形如 `[2026-07-29 14:30:00.123] [info] message`。
- **刷新策略**：`flush_on(info)`，info 及以上级别自动 flush，避免丢失重要日志。
- **初始化流程**：`main()` 中先创建 `QApplication`，再调用 `logging::init()`，确保 `QStandardPaths` 可用。

### 4. 使用约定与约束
- **统一宏接口**：业务代码应使用 `SIN_LOG_DEBUG/INFO/WARN/ERROR(tag, fmt_string, ...)`，第一个参数为模块 tag（如 `"DbcManager"`、`"FilterEngine"`），后续参数由 `fmt::format` 处理。
- **C++ 风格格式化**：宏内部通过 `fmt::format(__VA_ARGS__)` 将 Qt 字符串转为 `std::string` 后再传给 spdlog，避免直接混用 `QString` 与 `std::string`。
- **日志级别策略**：
  - 控制台默认 debug 级别，便于开发调试。
  - 文件 sink 设为 trace 级别，记录最详细信息。
  - 全局 logger 设为 debug 级别。
- **错误回退机制**：文件写入失败时捕获 `spdlog_ex` 异常并降级为仅控制台输出，保证程序健壮性。
- **线程安全**：两个 sink 均使用 `_mt` 后缀版本，支持多线程并发写入。

### 5. 实际使用情况
项目中已广泛使用 `SIN_LOG_*` 宏，覆盖 DBC 解析、过滤器编译、CAN 模拟器、UI 组件等模块；同时部分旧代码仍直接使用 `spdlog::info/warn/error`，尚未完全迁移到统一宏。