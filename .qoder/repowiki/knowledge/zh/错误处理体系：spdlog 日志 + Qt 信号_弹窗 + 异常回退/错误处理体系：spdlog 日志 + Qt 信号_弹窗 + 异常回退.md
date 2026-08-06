---
kind: error_handling
name: 错误处理体系：spdlog 日志 + Qt 信号/弹窗 + 异常回退
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/core/appconfig.cpp
    - src/core/projectmanager.cpp
    - src/core/filter_engine.h
    - src/core/filter_engine.cpp
    - src/core/candevicemanager.h
    - src/ui/mainwindow.cpp
    - src/core/canfileio/blf.cpp
---

本仓库的错误处理采用「分层策略」：核心 I/O 与解析层使用 C++ 异常进行错误传播，UI 层通过 Qt 信号 `errorOccurred` 和 `QMessageBox` 向用户反馈，所有路径统一经 spdlog 记录结构化日志。没有定义统一的自定义错误类型或返回值枚举，错误以「异常 + 日志 + UI 提示」三件套形式出现。

1. 日志系统（spdlog）
- 核心入口为 `src/core/logging.h/.cpp`，提供 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 宏，输出格式为 `[时间] [级别] [模块标签] 消息`。
- 初始化时创建控制台彩色 sink 与滚动文件 sink（5MB × 3），若目标目录不可写则自动回退到 `./logs`，失败时仅保留控制台输出。
- `src/utils/logging.h` 作为统一 re-export 头，强制所有模块通过该入口获取日志能力。
- 日志级别在 init 中设置为 debug，info 及以上自动 flush。

2. 异常与回退模式
- JSON 解析异常：`appconfig.cpp`、`projectmanager.cpp` 捕获 `json::parse_error`，记录错误后回退到默认值。
- spdlog 自身异常：`logging.cpp` 捕获 `spdlog::spdlog_ex`，降级为仅控制台输出。
- 通用 catch-all：部分位置使用 `catch (...) {}` 吞掉异常（如 `projectmanager.cpp`、`sidebarpanels.cpp`），保证 UI 不崩溃。
- 第三方库 dbcppp 抛出 `KCDParserError`、`NoRootElement` 等异常，由上层 try/catch 处理。

3. UI 层错误反馈
- 设备管理：`CanDeviceManager` 通过 `errorOccurred(const QString&)` 信号向上报告错误，`MainWindow` 连接该信号并弹出 `QMessageBox::warning`。
- 直接弹窗：各 UI 组件在文件打开失败、格式不支持、配置写入失败等场景直接调用 `QMessageBox::warning/information/question` 提示用户。
- 状态栏：`mainwindow.h` 中保留 `m_errorLabel` 用于显示错误信息。

4. 过滤引擎的错误表达
- `FilterEngine` 不抛异常，而是通过 `bool compile()` 返回编译结果，`QString errorString()` 返回人类可读的错误描述，调用方自行决定如何处理。

5. CAN 帧级错误标记
- `CanFrame` 结构体包含 `bool errorState` 字段，按 CAN 协议位标志错误帧，用于区分正常数据与总线错误帧。

6. 约定与约束
- 核心模块优先使用 `SIN_LOG_*` 宏而非 `qDebug/qWarning`；I/O 失败必须记录日志并通过信号或弹窗通知用户。
- 配置文件解析失败应记录错误并回退到默认值，而不是直接终止程序。
- UI 层禁止吞掉异常而不留痕迹，需至少记录日志或提示用户。
- 第三方库异常应在边界处捕获并转换为应用可理解的错误信息。