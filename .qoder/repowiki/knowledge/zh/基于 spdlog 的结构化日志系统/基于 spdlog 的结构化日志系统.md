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

本项目的日志系统基于第三方库 spdlog 构建，提供控制台彩色输出与滚动文件双 sink 的混合输出方案，并通过统一的 SIN_LOG_* 宏对外暴露。

## 1. 使用的框架与工具
- spdlog：作为底层日志引擎，通过 third_party/spdlog/ 集成。
- fmt：用于格式化字符串（fmt::format），在 SIN_LOG_* 宏中直接调用。
- Qt：用于获取应用数据目录（QStandardPaths::AppDataLocation）和创建日志目录。

## 2. 核心文件与包
- src/core/logging.h / src/core/logging.cpp：日志系统的初始化、关闭、默认 logger 获取以及 SIN_LOG_* 宏定义。
- src/utils/logging.h：对 core/logging.h 的重新包含，作为统一入口。
- src/main.cpp：在 QApplication 初始化后调用 logging::init()，程序退出前调用 logging::shutdown()。

## 3. 架构与约定
- 初始化流程：logging::init(logDir) 确定日志目录（默认 <AppData>/logs，不可写时回退到 ./logs），创建两个 sink：
  - stdout_color_sink_mt：控制台彩色输出，级别设为 debug。
  - rotating_file_sink_mt：滚动文件输出，单文件最大 5MB，保留 3 个历史文件，级别设为 trace。
- 全局 logger：名为 "sin" 的默认 logger 被设置为 spdlog 的全局默认 logger，格式为 [时间] [级别] 消息。
- 刷新策略：info 及以上级别自动 flush。
- 模块标识：每个 SIN_LOG_* 宏的第一个参数是 tag（模块名），如 "CanDeviceZLG"、"DBC"、"FilterEngine" 等，用于区分不同模块的日志来源。

## 4. 日志级别与使用约定
- 支持四个级别：DEBUG、INFO、WARN、ERROR，对应宏 SIN_LOG_DEBUG、SIN_LOG_INFO、SIN_LOG_WARN、SIN_LOG_ERROR。
- 所有日志均通过 SIN_LOG_* 宏调用，而非直接使用 spdlog::info 等 API（部分旧代码仍混用 spdlog::info/warn/error，但新代码统一使用宏）。
- 日志格式示例：[2026-07-29 14:30:00.123] [info] [DbcManager] message，其中 [info] 为级别，[DbcManager] 为 tag。
- 线程安全：sink 使用 _mt 后缀的多线程版本，保证多线程环境下的安全性。

## 5. 约束与规则
- 必须在 QApplication 设置应用名称之后调用 logging::init()（见 main.cpp 注释）。
- 程序退出前必须调用 logging::shutdown() 以确保缓冲刷新和资源释放。
- 日志目录不可写时会静默回退到当前目录的 ./logs，不会抛出异常中断程序。
- 文件 sink 初始化失败时仅降级为控制台输出，不崩溃。