---
kind: logging_system
name: 基于 spdlog 的 sin 日志系统（控制台 + 滚动文件双 sink）
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
- 日志后端：第三方库 `spdlog`（位于 `third_party/spdlog/`），通过 CMake 集成。
- 格式化：使用 `fmt::format` 进行参数化格式化，宏内部调用 `SPDLOG_*` 系列 API。
- 输出目标（sink）：
  - 控制台彩色输出：`spdlog::sinks::stdout_color_sink_mt`，级别设为 `debug`。
  - 滚动文件输出：`spdlog::sinks::rotating_file_sink_mt`，单文件上限 5MB，最多保留 3 个轮转文件，级别为 `trace`。
- Qt 集成：日志目录默认使用 `QStandardPaths::AppDataLocation/logs`；若不可写则回退到当前工作目录下的 `./logs`。

## 2. 核心文件
- `src/core/logging.h`：定义 `logging` 命名空间及 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 四个宏，声明 `init()`、`shutdown()`、`logger()`。
- `src/core/logging.cpp`：实现双 sink 初始化、默认 logger 注册、pattern 设置、flush 策略与关闭逻辑。
- `src/utils/logging.h`：薄包装头，重新 include `core/logging.h`，作为统一入口。
- `src/main.cpp`：在 `QApplication` 创建后调用 `logging::init()`，应用退出前调用 `logging::shutdown()`。

## 3. 架构与设计约定
- **全局单例式 logger**：`s_logger` 为静态 `std::shared_ptr<spdlog::logger>`，通过 `spdlog::set_default_logger` 注册为全局默认 logger，所有 `SPDLOG_*` 调用均路由到该实例。
- **初始化时机**：必须在 `QApplication` 构造之后、业务模块加载之前调用 `logging::init()`，以便 `QStandardPaths` 能正确解析 AppData 路径。
- **日志格式**：pattern 为 `[YYYY-MM-DD HH:MM:SS.mmm] [level] message`，由 `s_logger->set_pattern` 设定；宏中额外以 `[tag]` 形式包裹模块名，形成 `[时间] [级别] [模块] 消息` 的三段式结构。
- **级别策略**：
  - 控制台 sink 最低记录 `debug`，便于调试时观察。
  - 文件 sink 最低记录 `trace`，保留最详细轨迹供离线分析。
  - 默认 logger 级别设为 `debug`，并通过 `flush_on(info)` 在 info 及以上级别立即落盘。
- **容错回退**：当 `QStandardPaths::AppDataLocation/logs` 不可写时，捕获 `spdlog_ex` 并回退到 `./logs/sin.log`；若仍失败则仅保留控制台输出。
- **模块标识**：每个 `SIN_LOG_*` 调用必须传入第一个字符串参数作为 `tag`（如 `CanDeviceZLG`、`CanDeviceKvaser`、`CanDevicePEAK`），用于区分来源模块。
- **线程模型**：两个 sink 均为 `_mt`（多线程安全）版本，适合 Qt 多事件循环场景。

## 4. 使用约定与约束
- **统一入口**：业务代码应包含 `core/logging.h`（或通过 `utils/logging.h` 间接包含），禁止直接依赖 `spdlog` 原始头。
- **宏用法**：`SIN_LOG_INFO("模块名", "格式化字符串{}", 变量)`，内部委托给 `SPDLOG_INFO`，使用 `fmt::format` 风格占位符。
- **生命周期**：应用启动时调用 `logging::init()`，退出时调用 `logging::shutdown()`；`main.cpp` 已保证这一顺序。
- **日志位置**：默认写入 `<AppData>/sin/logs/sin.log`；可通过 `logging::init(logDir)` 指定自定义目录。
- **扩展点**：如需新增 sink（如网络/数据库），应在 `logging::init` 中追加到 `sinks` 向量并重新构建默认 logger。
- **现有使用范围**：目前主要在 `src/core/candevice*.cpp`（CAN 设备驱动层）中使用 `SIN_LOG_*` 记录设备打开/关闭、错误码等关键路径；UI 层尚未发现直接使用，表明日志主要服务于底层 CAN 通信与设备管理模块。

## 5. 与 Qt 原生日志的关系
项目未启用 `QtMessageHandler` 重定向，也未看到 `qDebug()` 被替换；日志体系完全独立于 Qt 的消息系统，通过 `spdlog` 直接输出。这避免了 Qt 日志与自定义 sink 之间的冲突，但也意味着 `qDebug()` 不会自动进入 `sin.log`。