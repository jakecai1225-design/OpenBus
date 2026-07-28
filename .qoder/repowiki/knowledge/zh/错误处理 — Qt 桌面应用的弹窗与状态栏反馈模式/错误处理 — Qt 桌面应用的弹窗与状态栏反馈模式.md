---
kind: error_handling
name: 错误处理 — Qt 桌面应用的弹窗与状态栏反馈模式
category: error_handling
scope:
    - '**'
source_files:
    - src/ui/mainwindow.cpp
    - src/ui/bottompanel.cpp
    - src/core/recorder.cpp
    - src/core/player.cpp
    - src/utils/canutils.cpp
---

本仓库是一个基于 Qt6 的 CAN/CAN FD 报文分析桌面软件，未定义统一的异常类型或错误码体系，而是采用 Qt 原生 API 的“返回值 + 用户提示”模式进行错误处理。

1. 系统/方法
- 核心 I/O 操作（文件读写、数据流解析）通过 `QFile::open()`、`QDataStream` 的布尔返回值判断成功与否，失败时直接返回 `false`，由调用方决定如何提示用户。
- UI 层使用 `QMessageBox`（warning/information/question）向用户弹出对话框，作为主要的人机交互错误反馈渠道。
- 底部面板 `BottomPanel` 提供三类输出通道：终端（只读 QPlainTextEdit）、输出日志（带时间戳）、问题列表（表格，支持严重度 0=信息、1=错误），用于结构化记录错误来源与描述。
- 主窗口状态栏包含独立的 `m_errorLabel`，可用于显示即时错误提示。

2. 关键文件与包
- `src/ui/mainwindow.cpp`：集中调用 `QMessageBox` 和 `BottomPanel::addProblem` 处理录制失败、文件加载失败、过滤语法错误等场景。
- `src/ui/bottompanel.cpp`：实现 `addProblem(severity, source, message)` 与 `appendOutput(text)`，是错误信息的统一汇聚点。
- `src/core/recorder.cpp` / `src/core/player.cpp`：I/O 失败时返回 `false`（如文件无法打开、魔术字/版本不匹配、数据流读取中断），不抛异常。
- `src/utils/canutils.cpp`：对 CAN FD ESI 错误帧字段进行识别与格式化，属于业务层面的“错误帧”标识而非程序错误。

3. 架构与约定
- 分层职责清晰：core 层仅通过布尔返回值表达失败，UI 层负责将失败转化为可理解的提示。没有跨层传递的错误对象。
- 错误分类通过字符串 `source` 字段区分来源（如 "Recorder"、"Player"、"DBC"、"Filter"），在 BottomPanel 的问题表中以列形式展示。
- 严重度采用整数 0/1/2（信息/警告/错误），当前代码中主要使用 0 和 1，颜色分别对应蓝/红。
- 对于需要用户确认的操作（退出时录制中、删除工程），使用 `QMessageBox::question` 获取 Yes/No 响应。

4. 约定与约束
- 未发现 `throw`/`catch`/`try` 块，C++ 异常未被启用或禁用，代码完全依赖返回值与 Qt 消息框。
- 未定义自定义错误类型、错误码枚举或全局错误处理器（如 `qSetMessagePattern`、`QException`）。
- 所有用户可见的错误都同时出现在两个位置：`QMessageBox` 弹窗 + `BottomPanel::addProblem` 问题表，形成双重反馈。
- 业务层的“错误帧”（CAN FD ESI、isErrorFrame）与程序错误分离，前者作为数据标志位处理，后者走上述 UI 反馈路径。