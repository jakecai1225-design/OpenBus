---
kind: error_handling
name: Qt/SPDlog 混合的错误处理：日志、返回码与 Qt 消息框
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/core/appconfig.cpp
    - src/core/projectmanager.cpp
    - src/core/canfileio/blf.cpp
    - src/core/file_import/asc_importer.cpp
    - src/core/file_import/blf_importer.cpp
    - src/core/file_import/csv_importer.cpp
    - src/core/candevicemanager.cpp
    - src/ui/deviceconnectiontab.cpp
    - src/ui/dbcimportdialog.cpp
---

## 1. 使用的系统与工具

- **结构化日志**：基于 `spdlog`，通过 `src/core/logging.h/.cpp` 暴露 `logging::init()` / `logging::shutdown()` / `logging::logger()`，并以 `SIN_LOG_DEBUG/INFO/WARN/ERROR(tag, ...)` 宏统一输出，格式为 `[时间] [level] [模块标签] 消息`。`src/utils/logging.h` 仅重新 include 该头文件作为统一入口。
- **Qt 调试输出**：在 I/O、解析等底层路径大量使用 `qDebug()` / `qWarning()`（如 BLF 解析失败、ASC/CSV/BLF 导入失败），用于快速定位问题。
- **用户可见错误**：UI 层使用 `QMessageBox::warning(...)` 向用户提示输入校验失败或 DBC 加载失败等场景。
- **异常捕获**：对第三方库抛出的异常进行局部 catch（`json::parse_error`、`spdlog::spdlog_ex`），捕获后记录日志并回退到默认值或静默忽略。

## 2. 关键文件与位置

| 文件 | 作用 |
|---|---|
| `src/core/logging.h` / `.cpp` | 日志系统初始化、`SIN_LOG_*` 宏定义 |
| `src/utils/logging.h` | 统一 re-include 核心日志头 |
| `src/core/appconfig.cpp` | JSON 配置解析的 `try/catch(json::parse_error)` 与 `spdlog::error` |
| `src/core/projectmanager.cpp` | 工程 JSON 解析的 `try/catch`（含 `catch(...) {}` 静默吞掉） |
| `src/core/canfileio/blf.cpp` | BLF 解析失败用 `qWarning()` 报告魔数/压缩/文件头异常 |
| `src/core/file_import/*.cpp` | ASC/CSV/BLF 导入器打开失败时 `qWarning()` + 返回空结果 |
| `src/ui/deviceconnectiontab.cpp` | 设备连接参数校验失败时 `QMessageBox::warning` |
| `src/ui/dbcimportdialog.cpp` | DBC 加载失败时 `QMessageBox::warning` |
| `src/core/candevicemanager.cpp` | 设备创建/打开失败通过 `emit errorOccurred(...)` 上抛 |

## 3. 架构与约定

### 3.1 分层职责

- **Core 层（I/O、解析、设备）**：不直接弹窗，而是通过 `qWarning()` / `spdlog` 记录错误，并通过返回值（`bool`、空容器、负计数）或信号（`errorOccurred`）向上传播。
- **UI 层**：负责把可恢复的用户输入错误转化为 `QMessageBox::warning`；对不可恢复的设备错误则消费 `errorOccurred` 信号并展示给用户。
- **日志层**：所有严重错误必须同时写入 `spdlog` 滚动文件 sink，便于事后排查。

### 3.2 错误传播模式

| 场景 | 传播方式 | 示例 |
|---|---|---|
| 配置文件解析失败 | `try { json::parse } catch (json::parse_error&) { spdlog::error; 使用默认配置 }` | `appconfig.cpp` L50-L56 |
| 工程 JSON 解析失败 | `try { ... } catch (...) {}`（静默忽略） | `projectmanager.cpp` L116 |
| 文件无法打开 / 格式不匹配 | `qWarning() << "..."; return false;` | `blf.cpp` L168-L170 |
| 解析过程中遇到坏数据 | `qWarning(); continue;`（跳过当前对象继续） | `blf.cpp` L280-L289 |
| 设备驱动缺失 / 打开失败 | `emit errorOccurred(QString)` | `candevicemanager.cpp` L84-L95 |
| UI 输入校验失败 | `QMessageBox::warning(this, title, msg); return;` | `deviceconnectiontab.cpp` L245-L265 |

### 3.3 未使用的方式

- 没有自定义 `Error` / `ErrorCode` 类型或枚举。
- 没有统一的 `Result<T, E>` 或 `expected` 风格封装。
- 没有 `throw std::exception` 的常规路径（仅在第三方库异常处 catch）。
- 没有 `panic/recover` 概念（C++ 中无此机制）。
- 没有全局错误中间件；错误处理是分散在各函数内的就地策略。

## 4. 约定与约束

- **日志优先**：任何“非预期但可恢复”的错误（文件损坏、魔数不匹配、zlib 解压失败）必须先 `qWarning()` / `spdlog::warn/error` 再返回错误码或跳过，禁止静默失败。
- **JSON 解析容错**：配置/工程 JSON 解析失败一律 `catch` 并回退到默认值，保证应用启动不崩溃。
- **UI 层只负责用户可见错误**：core 层不出现 `QMessageBox`，避免阻塞后台线程。
- **设备错误上抛**：`CanDeviceManager` 将底层设备错误包装为 `errorOccurred` 信号，由上层 UI 决定如何呈现。
- **解析器健壮性**：BLF/ASC/CSV 解析器对单条坏数据采用 `continue` 跳过而非中断整个文件，保证尽可能多地读取有效帧。
- **未实现功能显式拒绝**：例如 `BlfWriter::open` 打印 `qWarning("暂不支持 BLF 写入")` 并返回 `false`，调用方可据此判断。

## 5. 总结

该仓库采用 **Qt + spdlog 的混合错误处理方案**：内部诊断走 `SIN_LOG_*` 和 `qWarning`，用户可见错误走 `QMessageBox`，跨层错误通过返回值、空结果或 `errorOccurred` 信号传递。没有集中式的错误类型体系或全局中间件，错误处理以“就近记录 + 向上返回布尔/空值”的模式散布在各模块中。这种设计简单直接，适合桌面 CAN 分析工具的规模，但在需要精确区分错误原因的场景下缺乏结构化错误码支撑。