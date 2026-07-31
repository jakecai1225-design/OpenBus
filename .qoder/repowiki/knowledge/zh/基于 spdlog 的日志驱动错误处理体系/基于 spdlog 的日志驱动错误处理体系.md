---
kind: error_handling
name: 基于 spdlog 的日志驱动错误处理体系
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/core/appconfig.cpp
    - src/core/filter_engine.h
    - src/core/filter_engine.cpp
---

本仓库未采用统一的异常类型或错误码体系，而是以 **spdlog 结构化日志**为核心、辅以局部 `try/catch` 与返回值约定来传递错误信息。具体模式如下：

1. **日志即错误通道**
   - 核心日志模块位于 `src/core/logging.h` 与 `src/core/logging.cpp`，通过 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 宏输出带模块标签的结构化日志，同时写入控制台与滚动文件（默认 `./logs/sin.log`，5MB×3 轮转）。
   - `src/utils/logging.h` 仅作为统一入口重新包含 `core/logging.h`，确保 UI 层也使用同一日志器。
   - 初始化失败时（如日志目录不可写），`logging::init` 会捕获 `spdlog_ex` 并回退到 `./logs`，若仍失败则降级为仅控制台输出，保证程序可继续运行。

2. **I/O 与配置错误：`try/catch` + 降级策略**
   - `src/core/appconfig.cpp` 中 JSON 解析使用 `json::parse_error` 捕获，失败时记录 `spdlog::error` 并回退到默认配置；文件读写失败直接 `spdlog::warn/error` 后返回空值或默认值，不抛出异常。
   - 这种“记录 + 降级”的模式贯穿配置、日志等基础设施层。

3. **解析类错误：字符串错误码 + 状态查询**
   - `FilterEngine`（`src/core/filter_engine.{h,cpp}`）采用自写递归下降解析器，编译失败时通过 `m_impl->errorMsg` 保存错误文本，对外暴露 `isValid()` / `errorString()` 供调用方判断与展示，而非抛异常。
   - 第三方库 `dbcppp` 在 `third_party/dbcppp` 中也定义了 `enum class ErrorCode` 并通过 `getError()` 返回，项目内直接使用其错误码而非包装异常。

4. **UI 层无独立错误处理**
   - 搜索未发现 `qCritical/qWarning/QMessageBox/QErrorMessage` 的使用，UI 组件依赖上层返回的状态值与日志进行交互，未实现弹窗式错误提示。

5. **约束与约定**
   - 所有模块通过 `SIN_LOG_*` 宏记录错误，禁止裸 `printf`/`std::cerr`。
   - 解析/配置类函数以 `bool` 返回值 + `errorString()` 表达失败，不抛 C++ 异常。
   - 外部 I/O 操作使用 `try/catch` 捕获特定异常（`json::parse_error`、`spdlog_ex`），记录后降级而非中断流程。

该方案轻量、无额外依赖，适合桌面工具场景；但缺乏统一错误类型，跨模块错误传播依赖返回值约定与日志回溯。