---
kind: error_handling
name: 错误处理体系：spdlog 日志 + Qt 调试输出 + 异常捕获与状态码返回
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/utils/logging.h
    - src/core/appconfig.cpp
    - src/core/canfileio/blf.cpp
    - src/core/file_import/asc_importer.cpp
    - src/core/filter_engine.cpp
---

本仓库采用混合式错误处理策略，核心思路是「可恢复错误通过返回值/状态字段传播，不可恢复或诊断信息通过 spdlog 日志记录」，UI 层辅以 Qt 调试宏输出。具体表现如下：

1. **日志系统（主要诊断手段）**
   - 统一入口 `src/core/logging.h` 基于 spdlog 提供 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 宏，带模块 tag 和时间戳，同时写入控制台和滚动文件。
   - `src/utils/logging.h` 仅做重包含，作为全局统一入口。
   - 配置加载失败、JSON 解析异常等场景均通过 `spdlog::error/warn/info` 输出结构化日志，而非抛出异常或弹窗。

2. **异常捕获（第三方库边界）**
   - JSON 解析使用 `try/catch(json::parse_error)` 包裹，失败时回退到默认配置并记录错误日志（`appconfig.cpp`）。
   - BLF 读写通过 `try/catch(std::exception)` 捕获底层 Vector::BLF 库抛出的异常，打印到 `std::cerr` 后返回 `false` 或中断读取（`blf.cpp`）。
   - 部分内部循环使用 `catch(...)` 吞掉异常以保证批量处理的鲁棒性（如 BLF 对象读取循环中的单条记录失败跳过）。

3. **返回值与状态字段（业务错误传播）**
   - 文件导入器（ASC/BLF/CSV）在无法打开文件或解析失败时，通过 `qWarning()` 输出警告并返回空结果集，调用方需自行判断返回值是否为空。
   - 过滤器引擎 `FilterEngine` 将编译期语法错误以 `QString errorMsg` 成员保存，并通过 `errorString()` 暴露给 UI；`compile()` 返回 `bool` 指示是否成功。
   - BLF 读写器的 `open()` 返回 `bool`，`readAll()` 失败返回 `-1`，属于典型的 Qt-style 状态码模式。

4. **UI 层交互**
   - 未发现 `QMessageBox` 或 `QErrorMessage` 的使用，用户可见的错误主要通过日志窗口和界面状态反馈。
   - 未使用 `assert()` 进行运行时断言，也未见 `panic/recover` 语义（C++ 中即无此概念）。

5. **设计约定**
   - I/O 操作失败优先返回布尔值或特殊整数值，不向上抛出异常。
   - 解析类错误（JSON、BLF、过滤表达式）通过局部状态字段或返回值组合传递，并在关键路径上记录 `SIN_LOG_WARN/ERROR`。
   - 对第三方库的异常边界进行 try-catch 隔离，防止崩溃扩散到上层。