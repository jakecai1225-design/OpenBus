---
kind: frontend_style
name: Qt QSS 主题系统与 Web 原型样式
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - resources/styles/theme.qss
    - src/ui/thememanager.cpp
    - styles/theme.qss
    - UI/css/ui-prototype.css
    - UI/js/ui-loader.js
    - UI/js/ui-prototype.js
    - UI/ui-layout.md
---

## 概述

本项目采用 **双轨前端样式体系**：
- 运行时 Qt 桌面 UI 使用 **QSS（Qt Style Sheets）** + 自定义 `ThemeManager` 实现多主题切换；
- 独立 `UI/` 目录提供基于 HTML/CSS/JS 的 **Web 原型**，用于布局与交互预演。

## 1. Qt QSS 主题系统

### 核心文件
- `resources/styles/default.qss`：浅色 VS Code 风格默认主题，硬编码颜色值，作为基线样式。
- `resources/styles/theme.qss`：**主题模板**，全部颜色以 `@变量名` 占位符形式声明（如 `@windowBg`、`@accent`、`@textDim`），由 `ThemeManager` 在运行时替换。
- `src/ui/thememanager.cpp`：主题管理单例，维护 `Theme` 结构体（约 40 个颜色字段），内置 Light / Dark / VS Code Dark+ / VS Code Light+ / Monokai / Solarized Light / Solarized Dark 七套主题。
- `styles/theme.qss`：根级副本，供开发时直接编辑（`ThemeManager::generateQss` 优先从 `QDir::currentPath() + "/styles/theme.qss"` 读取，回退到 qrc `:/styles/theme.qss`）。

### 架构与约定
- 主题数据通过 `ThemeManager::initThemes()` 集中定义，新增主题只需复制一个 `Theme` 块并修改色值。
- `applyTheme(name)` 调用 `qApp->setStyleSheet(generateQss(theme))` 全局应用样式，无需重启。
- 变量映射表 (`varMap`) 将 `@xxx` 字符串与 `Theme` 成员指针绑定，`generateQss` 中遍历替换，形成“模板 + 数据”的解耦模式。
- 所有组件样式均通过 QSS 选择器覆盖：`QMainWindow`、`QMenuBar`、`QDockWidget`、`QTabBar`、`QToolBar`、`QLineEdit`、`QPushButton`、`QTableView`、`QScrollBar`、`QSplitter`、`QStatusBar`、`QGroupBox`、`QDialog`、`QPlainTextEdit` 等。
- 语义化 ID 选择器统一命名：`#CollapsibleTitle`、`#DockPanelTitle`、`#SidePanelTitle`、`#ProjectList`、`#ActivityBar`、`#BottomPanel`、`#RightPanel`、`#WindowButtons`、`#WinMinBtn`、`#WinCloseBtn`、`#TerminalOutput`、`#StatusWarn`、`#StatusOk`、`#StatusError` 等，确保同一视觉角色复用同一样式。
- 设计语言遵循 **VS Code 风格扁平·简洁·统一**（注释明确标注），强调无圆角/小圆角、细边框、浅灰背景、蓝色 `#0066b8` 强调色、深色终端区。

### 约束与规则
- 主题切换必须通过 `ThemeManager::applyTheme()` 进行，禁止在业务代码中直接 `qApp->setStyleSheet()` 绕过变量替换。
- 新增主题色需同时更新 `theme.qss` 中的 `@变量` 引用和 `thememanager.cpp` 的 `varMap` 映射，否则运行时不会生效。
- 开发阶段优先修改根目录 `styles/theme.qss`（热加载），生产构建打包进 qrc 的 `resources/styles/theme.qss`。

## 2. Web 原型样式（非生产 UI）

- `UI/css/ui-prototype.css`：纯 CSS 原型，使用 BEM 风格类名（`.scenario-bar`、`.menubar`、`.activitybar`、`.center-tabs`、`.trace-table`、`.dialog-overlay` 等），配色为深色系（`#1a1a1a`、`#2D2D2D`、`#4a90d9` 强调色）。
- `UI/js/ui-loader.js`、`UI/js/ui-prototype.js`：动态加载 `UI/partials/*.html` 片段，模拟主窗口布局（左栏/中央/右侧/底部 Dock）。
- `UI/ui-layout.md`：布局说明文档。
- 该部分仅用于交互与布局预演，不参与最终 Qt 应用构建。

## 3. 资源组织

- `resources/icons/*.svg`：项目内 SVG 图标资源，被 QSS 或 C++ 代码引用。
- `resources/resources.qrc`：Qt 资源清单，将样式与图标打包进可执行文件。
- `third_party/qcustomplot/`：图表库，其绘图样式由 QSS 间接影响（表格/滚动条等）。