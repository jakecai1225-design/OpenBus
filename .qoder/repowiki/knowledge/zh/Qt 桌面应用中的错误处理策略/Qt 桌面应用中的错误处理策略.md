---
kind: error_handling
name: Qt 桌面应用中的错误处理策略
category: error_handling
scope:
    - '**'
source_files:
    - src/ui/mainwindow.cpp
    - src/ui/filterbar.cpp
    - src/core/canframe.h
    - src/models/cantracemodel.cpp
    - src/utils/canutils.cpp
    - src/core/dbcmanager.cpp
    - src/ui/panels/sidebarpanels.cpp
---

该 sin CAN/CAN FD 分析工具基于 Qt6 + C++17 构建，错误处理采用以 Qt 原生机制为主的轻量级模式，未引入统一的异常框架或自定义错误类型体系。

**系统与方法**
- UI 层：通过 `QMessageBox`（warning/information/question）向用户直接弹出错误提示，如文件无法创建、加载失败、语法错误等；状态栏使用 `m_errorLabel` 显示错误信息，底部面板通过 `addProblem()` 记录问题。
- 日志输出：核心解析逻辑（如 DBC 解析）使用 `qDebug()` 打印调试信息，无结构化日志框架。
- 数据模型：CAN 帧的 `errorState` 字段和 `isErrorFrame()` 方法用于标识 CAN 总线错误帧，在 `CanTraceModel` 中根据错误帧设置红色前景色进行可视化区分。
- 第三方库：dbcppp 内部使用 `ErrorCode` 枚举（NoError 等）和 `setError()`/`getError()` 方法传递解析错误，但上层未统一消费这些错误码。

**关键文件与位置**
- `src/ui/mainwindow.cpp`：集中使用 `QMessageBox::warning()` 处理录制/打开/回放等操作失败，并调用 `m_bottomPanel->addProblem()` 记录问题。
- `src/ui/filterbar.cpp`：通过 `CanUtils::isFilterValid()` 实时校验过滤器语法，用图标和背景色反馈语法正确性，帮助信息通过 `QMessageBox::information()` 展示。
- `src/core/canframe.h`：定义 `errorState` 布尔字段和 `isErrorFrame()` 方法，作为 CAN 错误帧的数据标记。
- `src/models/cantracemodel.cpp`：在 `data()` 中根据 `isErrorFrame()` 返回红色前景色，实现错误帧的视觉高亮。
- `src/utils/canutils.cpp`：`formatFlags()` 将 `errorState` 和 `isErrorFrame()` 转换为 "ESI"/"ERR" 标志字符串。
- `src/core/dbcmanager.cpp`：使用 `qDebug()` 输出解析过程日志，解析失败时静默跳过行而非抛出异常。
- `src/ui/panels/sidebarpanels.cpp`：文件保存失败时使用 `QMessageBox::warning()` 提示，删除操作前用 `QMessageBox::question()` 确认。

**架构与约定**
- 错误传播方式：UI 层直接调用 QMessageBox 弹窗，不向上抛出异常；核心逻辑（DBC 解析、过滤器解析）通过返回值（bool）或空结果表示失败，由调用方决定如何反馈。
- 无统一错误类型：未发现自定义 error class、sentinel error 或 std::exception 派生类；错误信息以 QString 形式硬编码在 QMessageBox 标题和内容中。
- 无 panic/recover：C++ 代码未使用 throw/try/catch（除 third_party/dbcppp 主程序外），依赖返回值和 Qt 信号槽机制传递状态。
- 错误分类：UI 层区分了警告（warning）、信息（information）、确认（question）三类对话框，分别对应不同严重程度的错误场景。

**约束与限制**
- 错误信息语言为中文硬编码，未做国际化处理。
- 底层解析错误（如 DBC 格式错误）仅通过 qDebug 输出，不会中断程序执行，可能导致后续逻辑使用不完整数据。
- 过滤器解析器（Lexer/Parser）在语法错误时返回空 FilterPredicate，调用方需自行检查有效性。