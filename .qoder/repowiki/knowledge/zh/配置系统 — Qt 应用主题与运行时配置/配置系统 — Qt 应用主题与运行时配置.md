---
kind: configuration_system
name: 配置系统 — Qt 应用主题与运行时配置
category: configuration_system
scope:
    - '**'
source_files:
    - src/main.cpp
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - src/ui/panels/sidebarpanels.h
---

本仓库未实现通用的应用程序配置文件加载机制（如 .ini/.json/.yaml/env），而是将“配置”概念集中在两个层面：

1. **主题配置（ThemeManager）**：所有 UI 外观颜色、样式均通过 `src/ui/thememanager.h/.cpp` 中的 `ThemeManager` 单例管理。主题以 C++ 结构体 `Theme` 硬编码在 `initThemes()` 中，包含 Light、Dark、VS Code Dark+/Light+、Monokai、Solarized Light/Dark 等预设，通过 `generateQss()` 动态生成 QSS 字符串并调用 `qApp->setStyleSheet()` 生效。默认主题为 "Light"，由 `main.cpp` 启动时调用 `ThemeManager::instance()->applyTheme("Light")` 设置。

2. **用户界面配置**：侧边栏的 SettingsPanel 提供主题切换入口，通过 `SettingsPanel::themeChanged` 信号触发 `MainWindow::onSettingsRequested` 进而调用 `ThemeManager::applyTheme(name)` 实时切换。此外，`ProjectContext` 结构体中包含 `layoutConfig` 字段用于保存布局配置，但当前未见持久化实现。

3. **Qt 应用元信息**：`main.cpp` 通过 `QApplication::setOrganizationName/ ApplicationName/ ApplicationVersion` 设置应用标识，这些值会被 Qt 的 `QSettings` 用作默认存储路径前缀，但项目中未实际使用 `QSettings` 进行读写。

4. **资源样式**：`resources/styles/default.qss` 和 `resources/resources.qrc` 提供默认 QSS 样式表，但主题系统并未加载该文件，而是完全依赖内存中的 Theme 数据。

**约束与约定**：
- 主题必须通过 `ThemeManager::applyTheme()` 切换，禁止直接修改 `qApp->styleSheet()`。
- 新增主题需在 `initThemes()` 中按相同结构体格式添加，并在 `applyTheme()` 的匹配逻辑中支持。
- 项目级配置（如 layoutConfig）目前仅存在于内存结构体，无持久化机制。
- 未使用环境变量、命令行参数或外部配置文件作为应用配置来源。