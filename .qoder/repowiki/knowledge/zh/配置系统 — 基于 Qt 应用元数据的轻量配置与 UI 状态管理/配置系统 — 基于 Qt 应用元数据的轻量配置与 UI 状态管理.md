---
kind: configuration_system
name: 配置系统 — 基于 Qt 应用元数据的轻量配置与 UI 状态管理
category: configuration_system
scope:
    - '**'
source_files:
    - src/main.cpp
    - src/ui/signalconfigdialog.h
    - src/ui/panels/sidebarpanels.h
    - src/ui/mainwindow.cpp
---

本仓库未实现独立的配置文件加载系统（如 YAML/JSON/TOML/env），而是采用 Qt 框架内置机制进行轻量级运行时配置，主要体现为以下三个方面：

1. **应用元数据配置**：在 `src/main.cpp` 中通过 `QApplication::setApplicationName("sin")`、`QApplication::setOrganizationName("sin")`、`QApplication::setApplicationVersion("0.1.0")` 设置应用标识，这是 Qt 平台持久化存储（如 QSettings）的基础。样式表通过 `QFile` 从 Qt 资源文件 `:styles/default.qss` 动态加载。

2. **UI 状态与信号配置**：图形视图的信号监控配置通过 `SignalConfigDialog` 对话框收集（信号名称、CAN ID、扩展帧、字节偏移、位长、字节序等），并以内存中的 `GraphicView::Signal` 结构体传递，尚未发现持久化到文件的逻辑。

3. **项目上下文与布局配置**：`ProjectContext` 结构体包含项目名称、DBC 文件列表、录制文件列表和 `layoutConfig`（字符串字段），但当前仅作为内存数据结构存在，未见读写实现。

4. **设置面板占位**：`SettingsPanel` 提供“配置”入口，但 `MainWindow::onSettingsRequested` 仅显示提示框 `(待实现)`，表明用户设置持久化功能尚未开发。

**约束与约定**：
- 所有配置目前均为内存态，无跨进程或跨会话持久化能力。
- 样式表统一通过 Qt 资源系统加载，路径硬编码为 `:styles/default.qss`。
- 设备连接参数（通道、波特率、FD 模式、设备类型）通过 UI 控件直接传入，未抽象为独立配置模块。

该项目的配置系统处于早期阶段，核心业务逻辑优先于配置持久化功能的实现。