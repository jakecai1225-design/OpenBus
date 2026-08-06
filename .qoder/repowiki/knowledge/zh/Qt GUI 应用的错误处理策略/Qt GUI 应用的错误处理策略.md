---
kind: error_handling
name: Qt GUI 应用的错误处理策略
category: error_handling
scope:
    - '**'
source_files:
    - src/ui/mainwindow.cpp
    - src/core/player.cpp
    - src/core/recorder.cpp
    - src/core/dbcmanager.cpp
    - src/utils/canutils.h
    - src/core/canframe.h
---

本仓库是一个基于 Qt6 的 CAN/CAN FD 报文分析桌面应用，其错误处理方式遵循典型的 Qt GUI 应用模式，未建立统一的错误类型体系或异常机制，而是采用分层、分散的处理策略：

**1. 核心层（core/models/utils）——布尔返回值 + 日志输出**
- `Player::load()`、`Recorder::start()` 等核心 I/O 操作通过返回 `bool` 表示成功/失败，调用方在 UI 层捕获并提示。
- DBC 解析使用 `qDebug()` 输出调试信息（如 `[DBC] Parsing:`、`[DBC] Parsed:`），无结构化错误码。
- `CanFrame::isErrorFrame()` 通过位标志检测 CAN 错误帧，属于数据层面的错误标识而非程序异常。
- `CanUtils::parseFilter()` 返回空谓词函数表示解析失败，由调用方判断并提示。

**2. UI 层（ui/mainwindow.cpp 等）——QMessageBox + 状态栏 + 底部面板**
- 用户可见的错误统一通过 `QMessageBox::warning()` 弹窗提示（如「无法加载」「无法创建文件」）。
- 操作结果和错误同时写入底部面板：`m_bottomPanel->appendOutput()` 记录成功信息，`m_bottomPanel->addProblem(level, source, message)` 记录问题。
- 状态栏中专门预留 `m_errorLabel` 用于显示错误状态。
- 关键交互使用 `QMessageBox::question()` 进行确认（如退出时询问录制状态）。

**3. 第三方库集成（dbcppp）——枚举错误码**
- 依赖的 dbcppp 库定义了 `ErrorCode` 枚举（NoError 等），通过 `getError()` 方法获取解析状态，但本项目未广泛使用该机制。

**4. 设计约束与约定**
- 无全局异常处理器、无自定义异常类、无 panic/recover 机制。
- 错误传播路径短：核心层返回 bool → UI 层捕获并展示，避免深层堆栈展开。
- 所有用户可感知的错误都经过双重反馈：弹窗 + 底部面板日志，确保用户不会遗漏重要错误。
- 过滤器语法错误等输入验证错误通过 `isFilterValid()` 预检查，失败时在底部面板标注「语法错误」。