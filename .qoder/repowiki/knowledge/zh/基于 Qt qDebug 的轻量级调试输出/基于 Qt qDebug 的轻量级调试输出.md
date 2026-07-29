---
kind: logging_system
name: 基于 Qt qDebug 的轻量级调试输出
category: logging_system
scope:
    - '**'
source_files:
    - src/core/dbcmanager.cpp
    - src/main.cpp
---

本仓库未实现专门的日志系统，仅使用 Qt 框架自带的 `qDebug()` 进行简单的调试信息输出。具体表现如下：

1. **使用的框架与工具**：仅依赖 Qt 的 `QDebug`（通过 `#include <QDebug>` 隐式引入），未集成任何第三方日志库（如 spdlog、glog、boost.log 等）。虽然 third_party/dbcppp 子模块中使用了 `boost/log/trivial.hpp`，但这是第三方库自身的日志，不属于本项目。

2. **关键文件与位置**：所有日志输出集中在 `src/core/dbcmanager.cpp` 中，用于 DBC 文件解析过程的调试，例如：
   - 第 445 行：`qDebug() << "[DBC] Parsing:" << out.fileName << "lines:" << mergedLines.size();`
   - 第 726-730 行：`qDebug() << "[DBC] Parsed:" << out.messages.size() << ...;`

3. **架构与约定**：
   - 没有统一的日志初始化或配置入口（`main.cpp` 中未设置 `QtMsgHandler`）。
   - 没有日志级别管理，全部使用 `qDebug()`，无法区分 debug/info/warning/error。
   - 没有结构化字段，输出为简单的字符串拼接形式。
   - 没有日志文件或控制台输出路由配置，默认输出到 Qt 消息处理器（通常显示在 IDE 输出窗口）。

4. **约束与现状**：当前项目处于早期开发阶段（版本 0.1.0），日志功能尚未系统化设计，仅以临时调试为目的散落在核心解析逻辑中。