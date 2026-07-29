---
kind: configuration_system
name: 配置系统 — 基于 Qt 内置机制与内存主题的配置管理
category: configuration_system
scope:
    - '**'
source_files:
    - src/main.cpp
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - src/ui/panels/sidebarpanels.cpp
---

本仓库未实现独立的配置文件加载系统（如 .ini/.json/.yaml/.env 等），而是采用以下轻量级配置方式：

1. **应用元信息**：在 `src/main.cpp` 中通过 `QApplication::setApplicationName/OrganizationName/ApplicationVersion` 设置应用名称、组织名和版本号，仅用于标识，不持久化。

2. **主题配置**：由 `src/ui/thememanager.h/.cpp` 中的 `ThemeManager` 单例管理。主题以 C++ 结构体 `Theme` 硬编码在 `initThemes()` 中（Light、Dark、VS Code Dark+/Light+、Monokai、Solarized Light/Dark 共 6 套），通过 `applyTheme(name)` 动态生成 QSS 样式表并应用到 `qApp`。当前主题名保存在成员变量 `m_currentName` 中，但代码中未见持久化到磁盘的逻辑。

3. **项目/工程配置**：`ProjectPanel` 将工程信息（名称、DBC 文件列表、录制文件列表）以自定义文本格式保存到 `.sinproj` 文件中（键值对 + 段落形式），这是唯一可见的持久化配置写入逻辑。

4. **信号图形配置**：`GraphicView::Signal` 结构体包含 `config` 字段（颜色、CAN ID、扩展帧标志等），但这些配置仅在运行时内存中维护，未发现序列化/反序列化代码。

5. **无全局配置存储**：全仓搜索 `QSettings`、`.ini`、`.json`、`.yaml`、`.toml`、`.env` 均未找到相关使用。所有“设置”入口（`SettingsPanel`）目前仅提供主题切换 UI，尚未实现实际配置项的读写。

**结论**：该项目处于早期阶段，配置系统尚未完善——仅有主题切换的运行时内存管理和工程文件的简单文本保存，缺少统一的配置框架、环境变量注入、配置文件加载与持久化机制。