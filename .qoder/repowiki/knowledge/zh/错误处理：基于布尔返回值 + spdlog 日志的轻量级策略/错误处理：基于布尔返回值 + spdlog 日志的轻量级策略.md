---
kind: error_handling
name: 错误处理：基于布尔返回值 + spdlog 日志的轻量级策略
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/core/dbc/dbc_adapter.h
    - src/core/dbc/dbc_adapter.cpp
    - src/core/dbcmanager.h
    - src/core/dbcmanager.cpp
    - src/ui/dbcimportdialog.cpp
    - src/ui/signalsendtab.cpp
    - src/core/appconfig.cpp
    - src/core/canfileio/blf.cpp
    - src/core/file_import/asc_importer.cpp
    - src/core/file_import/blf_importer.cpp
    - src/core/file_import/csv_importer.cpp
---

## 1. 整体方法
本仓库没有统一的异常类型体系或错误码枚举，而是采用 Qt/C++ 工程常见的**“布尔返回值 + 日志记录”**模式：
- 可恢复的错误（如 DBC 文件无法打开、JSON 解析失败）通过函数返回 `bool` / 空容器 / `nullptr` 向调用方传递；
- 不可恢复或诊断性信息通过 `spdlog` 包装宏 `SIN_LOG_*` 输出到控制台与滚动日志文件；
- UI 层对关键用户操作使用 `QMessageBox::warning` 直接弹窗提示。

该方案简单直接，适合桌面工具类应用，但缺乏结构化错误对象、错误传播链和统一错误码。

## 2. 关键文件与位置
- **日志基础设施**：`src/core/logging.h` / `src/core/logging.cpp`，提供 `logging::init()` / `shutdown()` 及 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 宏，基于 `spdlog` 双 sink（控制台彩色 + 5MB×3 滚动文件），初始化失败时回退到当前目录 `./logs`。
- **DBC 解析适配层**：`src/core/dbc/dbc_adapter.h` / `.cpp`，`parse()` 返回 `bool` 表示解析成功与否；内部使用 `qDebug()` / `qWarning()` 输出解析过程与错误（魔数错误、头长度异常、解压失败等）。
- **DBC 管理器**：`src/core/dbcmanager.h` / `.cpp`，`loadDbc()` 调用 `dbc::parse()` 并返回 `bool`，失败时上层仅收到 `false`。
- **UI 层错误反馈**：`src/ui/dbcimportdialog.cpp` 中 `onImportFromFile()` 调用 `m_dbcManager->loadDbc(path)`，失败时弹出 `QMessageBox::warning(this, "导入失败", ...)`。
- **配置加载**：`src/core/appconfig.cpp` 捕获 `nlohmann::json::parse_error` 并记录日志后返回默认值。
- **文件 I/O 模块**：`src/core/canfileio/blf.cpp`、`src/core/file_import/*.cpp` 广泛使用 `qWarning()` 报告无法打开文件、魔数不匹配、压缩方法不支持等情况。

## 3. 架构与约定
- **分层职责清晰**：底层解析器（`dbc_adapter`、`canfileio`）只负责返回布尔状态并记录详细日志；中间层（`DbcManager`）将底层结果转换为业务语义（`emit dbcLoaded(...)` 信号）；UI 层根据布尔值决定弹窗或静默忽略。
- **无异常抛出**：核心路径未使用 C++ `throw` 进行控制流（仅在 JSON 解析处捕获第三方库异常），避免跨模块异常传播成本。
- **日志即错误载体**：当调用方不检查返回值时，`SIN_LOG_ERROR` / `qWarning` 仍是唯一可见的错误出口。日志格式统一为 `[时间] [级别] 消息`，便于集中排查。
- **UI 交互错误即时反馈**：对用户可感知的失败（如 DBC 导入失败），使用模态 `QMessageBox` 阻断式提示；对后台任务失败（如文件读取）则仅记录日志。

## 4. 约定与约束
- **可恢复错误一律返回 `bool`**：`DbcManager::loadDbc`、`dbc::parse`、各 importer 的 `load` 接口均以 `true/false` 表达成功/失败，调用方需显式判断。
- **日志标签化**：通过 `SIN_LOG_*` 宏的第一个参数作为模块 tag（如 `"DbcManager"`），便于过滤日志。
- **日志文件自动轮转**：`logging::init` 固定使用 5MB × 3 个文件轮转，若目标目录不可写则降级到 `./logs`，确保日志始终可写。
- **UI 层禁止吞掉错误**：`DbcImportDialog::onImportFromFile` 在 `loadDbc` 失败时必须弹出 `QMessageBox::warning`，不得静默返回。
- **缺失资源以空值安全返回**：`findMessage` / `findSignal` 返回 `const Dbc*` 指针，找不到时返回 `nullptr`，调用方应判空（见 `SignalSendTab::onIdEditingFinished` 中对 `msg` 的空指针保护）。
- **未定义行为就地防御**：如 `DbcSignal::encode` 中先 `while (data.size() < neededBytes) data.append(0)` 再写入位，避免越界；`rawDecode` 中逐位检查 `byteIdx` 范围后再访问。

## 5. 局限性与观察到的缺口
- 没有统一的错误类型/错误码枚举，不同模块各自用 `bool` + 日志描述错误原因，难以聚合统计。
- 部分路径未检查返回值（如 `SignalSendTab::onRowSend` 中 `item(row, 6)->text().toInt()` 可能得到 0），潜在崩溃风险由 Qt 框架兜底。
- 日志级别混用 `qDebug` / `qWarning` / `SIN_LOG_*`，尚未完全迁移到统一的 `SIN_LOG_*` 宏体系。
- 没有全局错误处理器或中间件机制，错误传播依赖调用栈手动向上返回布尔值。

总体而言，这是一个面向桌面工具的轻量级错误处理方案：**底层返回布尔 + 打日志，UI 层弹窗告警**，结构简单、易于理解，但在大规模工程中可扩展性有限。