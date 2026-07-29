---
kind: logging_system
name: 日志系统：基于 Qt qDebug 的轻量调试输出
category: logging_system
scope:
    - '**'
source_files:
    - src/core/dbcmanager.cpp
    - src/main.cpp
---

本仓库未实现专门的日志框架或结构化日志系统，仅在 DBC 解析模块中使用 Qt 内置的 `qDebug()` 进行简单的调试输出。

**使用的系统与工具**
- 仅依赖 Qt 标准库中的 `<QDebug>`，通过 `qDebug() << ...` 形式输出文本日志。
- 未发现任何第三方日志库（如 spdlog、glog、Boost.Log 等）的使用。
- 未注册自定义消息处理器（`qInstallMessageHandler`），也未使用 `QLoggingCategory` 进行分类控制。

**关键文件与位置**
- `src/core/dbcmanager.cpp`：唯一包含 `qDebug()` 调用的文件，在 DBC 文件解析的关键路径上输出两条调试信息：
  - L445：`qDebug() << "[DBC] Parsing:" << out.fileName << "lines:" << mergedLines.size();`
  - L726：`qDebug() << "[DBC] Parsed:" << out.messages.size() << "messages," << out.nodes.size() << "nodes," << out.valueTables.size() << "valueTables," << out.attributeDefs.size() << "attrDefs," << out.attributeValues.size() << "attrValues";`
- `src/main.cpp`：应用入口，未设置任何全局日志处理器或类别配置。

**架构与约定**
- 日志输出采用 Qt 默认的文本流式拼接方式，无结构化字段、无时间戳、无级别区分。
- 所有日志均使用 `qDebug()`，即 Debug 级别，没有 `qInfo`/`qWarning`/`qCritical` 的分级使用。
- 日志内容以 `[DBC]` 前缀标识来源模块，属于最基础的模块标记约定。
- 日志仅用于开发调试阶段，未集成到 UI 界面中作为用户可见的输出通道。

**约束与限制**
- 由于未安装消息处理器，日志默认输出到 Qt Creator 的 Application Output 窗口或控制台。
- 无法通过环境变量或配置文件动态调整日志级别或输出目标。
- 生产构建中可通过编译选项 `-DQT_NO_DEBUG_OUTPUT` 禁用所有 `qDebug()` 输出，但当前代码未做条件编译保护。
- 日志格式固定为 Qt 默认的 `[%{time}] %{appname} %{pid} %{category} %{message}` 样式，不可定制。