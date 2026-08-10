---
kind: frontend_style
name: 基于 Qt QSS + @变量主题系统的 VS Code 风格 UI 样式体系
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - resources/styles/theme.qss
    - styles/theme.qss
    - src/ui/thememanager.cpp
    - UI/css/ui-prototype.css
    - UI/ui-prototype.html
    - UI/js/ui-loader.js
    - UI/js/ui-prototype.js
    - UI/partials/menubar.html
    - UI/partials/left-dock.html
    - UI/partials/right-dock.html
    - UI/partials/bottom-dock.html
    - UI/partials/center-area.html
    - UI/partials/dialogs.html
    - UI/partials/overlays.html
    - UI/partials/scenario-bar.html
---

## 1. 系统/方法概述

本项目采用 **Qt6 + Qt StyleSheet (QSS)** 作为桌面端 UI 样式方案，整体视觉风格明确对标 **VS Code**（扁平、简洁、统一），并通过自研的 `ThemeManager` 实现运行时可切换的主题系统。此外，项目还包含一个独立的纯 HTML/CSS/JS 浏览器原型（`UI/`），用于在 Qt 实现前验证布局与交互。

- **生产 UI**：C++/Qt 应用，通过 `qApp->setStyleSheet()` 全局注入 QSS。
- **原型 UI**：`UI/ui-prototype.html` + `UI/css/ui-prototype.css` + `UI/js/ui-loader.js`，使用局部片段加载（`partials/*.html`）模拟 Dock/菜单栏/标签页等布局。
- **设计语言**：扁平化、无圆角或极小圆角（2px）、强调色 `#0066b8`（亮）/ `#0e639c`（暗）、状态栏蓝色条、侧边 ActivityBar 48px 宽、面板标题统一 28px 高。

## 2. 核心文件与包

| 文件 | 作用 |
|---|---|
| `resources/styles/default.qss` | 默认浅色主题硬编码 QSS（直接写死颜色值，供开发期/回退使用） |
| `resources/styles/theme.qss` | QSS 模板，全部颜色以 `@变量` 占位，由 ThemeManager 替换 |
| `styles/theme.qss` | 与上同名的文件系统副本，优先从磁盘读取（热更新无需重新编译） |
| `src/ui/thememanager.cpp` | 主题管理器：内置 Light/Dark/VS Code Dark+/Light+/Monokai/Solarized Light/Dark 共 7 套主题，将 `@变量` 映射到 `Theme` 结构体字段并生成最终 QSS |
| `UI/css/ui-prototype.css` | 浏览器原型样式，独立于 Qt 样式体系，用于界面布局预演 |
| `UI/ui-prototype.html` + `UI/partials/*.html` | 原型页面骨架与分片（菜单栏、左右 Dock、底部 Dock、对话框、覆盖层等） |
| `UI/js/ui-loader.js` / `ui-prototype.js` | 原型 JS：动态加载 partials、处理菜单/标签页/对话框交互 |
| `resources/resources.qrc` | Qt 资源索引，注册 SVG 图标与样式资源 |

## 3. 架构与约定

### 3.1 主题系统（ThemeManager）
- `ThemeManager::initThemes()` 在构造时注册 7 套主题，每套主题定义约 40 个语义化颜色字段（如 `windowBg`、`contentBg`、`sidebarBg`、`panelBg`、`barBg`、`accent`、`selectionBg`、`statusBg`、`terminalBg`、`scrollHandle` 等）。
- `generateQss()` 先尝试从 `./styles/theme.qss` 读取（开发模式），再回退到 qrc 中的 `:styles/theme.qss`；然后通过 `QHash<QString, QString Theme::*>` 将 `@变量` 一一替换为对应颜色的十六进制值，最后调用 `qApp->setStyleSheet()` 生效。
- 新增主题只需在 `initThemes()` 追加一组颜色值，无需修改任何 QSS。

### 3.2 QSS 模板约定
- 所有颜色不得硬编码，必须使用 `@变量` 占位符（如 `@windowBg`、`@textDim`、`@accentHover`、`@closeBtnHover` 等），确保同一份样式可在多主题间复用。
- 组件选择器按功能分区组织：菜单栏、DockWidget、列表/树、工具栏、标签页（主区域/底部/右侧三处）、输入框、表格、分割器、滑块、下拉框、状态栏、复选框、GroupBox、对话框、SpinBox、文本编辑、滚动条、窗口控制按钮。
- 字体统一为 `Segoe UI, Microsoft YaHei UI, sans-serif`，正文 `font-size: 12px`，辅助文字 `11px`，代码区使用 `Consolas, "Courier New", monospace`。

### 3.3 原型 UI 约定
- 原型 CSS 使用 BEM 风格类名（如 `.scenario-bar`、`.menu-item`、`.ctab`、`.dock-pane`），不依赖框架。
- 通过 `ui-loader.js` 动态加载 `partials/` 下的 HTML 片段，模拟真实应用的 Dock/菜单栏/标签页行为。
- 原型与 Qt 实现在布局上保持对齐（ActivityBar 48px、面板标题 28px、标签页高度一致等），便于对照验证。

## 4. 约定与约束

- **运行时换肤**：用户切换主题时通过 `ThemeManager::applyTheme(name)` 重新生成 QSS 并应用到整个 `QApplication`，无需重启程序。
- **开发热更新**：QSS 模板优先从工作目录 `styles/theme.qss` 读取，修改后重启即可生效，避免每次改样式都需重新编译。
- **颜色集中管理**：所有 UI 颜色必须走 `Theme` 结构体字段，禁止在业务代码中散落硬编码颜色值。
- **组件命名空间**：QSS 大量使用 `#ObjectId` 选择器（如 `#CollapsibleTitle`、`#ProjectList`、`#ActivityBar`、`#BottomPanel`、`#RightPanel`、`#TerminalOutput`、`#WindowButtons`、`#WinCloseBtn` 等），要求 C++ 侧为关键控件设置 `setObjectName()`。
- **状态语义颜色**：`#StatusOk`（绿色 `#2d8a3e`）、`#StatusWarn`（橙色 `#d68a1c`）、`#StatusError`（红色 `#d63232`）、`#StatusRec`（录制红 `#d63232`）在各主题下保持一致语义。
- **原型与实现分离**：`UI/` 目录是独立的前端原型，不参与 Qt 构建；真正的 UI 样式只存在于 `resources/styles/` 和 `src/ui/thememanager.cpp` 中。
- **无第三方样式库**：未引入 Bootstrap、Tailwind、Ant Design 等前端样式框架，完全手写 QSS。
- **图标资源**：SVG 图标集中在 `resources/icons/`，通过 Qt 资源系统引用，不在 CSS 中内联 base64。
