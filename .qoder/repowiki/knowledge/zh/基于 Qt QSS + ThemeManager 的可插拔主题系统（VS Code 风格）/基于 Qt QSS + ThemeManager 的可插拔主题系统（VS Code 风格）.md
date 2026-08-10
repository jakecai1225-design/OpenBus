---
kind: frontend_style
name: 基于 Qt QSS + ThemeManager 的可插拔主题系统（VS Code 风格）
category: frontend_style
scope:
    - '**'
source_files:
    - src/ui/thememanager.cpp
    - styles/theme.qss
    - resources/styles/default.qss
    - resources/styles/theme.qss
    - UI/css/ui-prototype.css
    - UI/ui-prototype.html
---

## 1. 采用的样式体系

本项目是 Qt/C++ 桌面应用，前端样式完全通过 **Qt StyleSheet (QSS)** 实现，并配合自研的 `ThemeManager` 提供运行时主题切换。整体视觉风格对标 **VS Code**：扁平、简洁、暗/亮双色调，强调侧边栏-主区域-底部面板-右侧面板的经典 IDE 布局。

除 Qt 原生界面外，项目还保留了一个独立的 HTML/CSS 原型页面（`UI/ui-prototype.html` + `UI/css/ui-prototype.css`），用于在浏览器中快速预览布局与交互；该原型与 Qt 运行时的 QSS 两套样式并存，但互不依赖。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `src/ui/thememanager.cpp` | 主题引擎：定义 Light / Dark / VS Code Dark+ / VS Code Light+ / Monokai / Solarized Light / Solarized Dark 七套色板，运行时将模板中的 `@变量` 替换为具体颜色并调用 `qApp->setStyleSheet()` |
| `styles/theme.qss` | 开发模式可热重载的 QSS 模板，使用 `@windowBg`、`@accent` 等占位符，由 `ThemeManager::generateQss` 解析 |
| `resources/styles/default.qss` | 内置默认样式表（硬编码十六进制值），作为未找到文件系统模板时的回退样式 |
| `resources/styles/theme.qss` | 打包进 qrc 的 QSS 模板副本（与 `styles/theme.qss` 内容一致） |
| `UI/css/ui-prototype.css` | 浏览器端原型 CSS，独立于 Qt 样式系统 |
| `UI/ui-prototype.html` + `UI/partials/*.html` | 原型页面的骨架与片段 |

## 3. 架构与设计约定

### 3.1 设计令牌（Design Tokens）
`ThemeManager::initThemes` 中每套主题都声明一组语义化 token：`windowBg`、`contentBg`、`sidebarBg`、`panelBg`、`barBg`、`text`、`textDim`、`accent`、`border`、`selectionBg`、`hoverBg`、`buttonBg`、`statusBg`、`terminalBg`、`tabBg`、`scrollHandle`、`closeBtnHover`、`headerBg`、`altRowBg` 等。这些 token 在 `generateQss` 中以 `@tokenName` 形式映射到 C++ 成员指针，形成“模板 → 主题实例”的单向数据流。

### 3.2 模板优先策略
`applyTheme` 调用 `generateQss` 时，先尝试从当前工作目录读取 `styles/theme.qss`（开发模式改完无需重新编译），找不到再回退到资源文件 `:styles/theme.qss`。这意味着生产构建会嵌入 qrc 版本，而开发者可在工程根目录下覆盖该文件进行热调试。

### 3.3 组件级样式约定
- **菜单栏**：同时充当自定义标题栏，高度固定 28–32px，背景 `@barBg`，选中态用 `@accent`。
- **ActivityBar**：左侧 48–50px 窄条，选中项以左边框 `@accent` 高亮。
- **DockWidget**：左/右/下三个 Dock 统一使用 `@sidebarBg`，标题区使用 `#CollapsibleTitle` / `#DockPanelTitle` / `#SidePanelTitle` 等 ID 选择器。
- **标签页**：主区域 `QTabBar::tab` 使用 `@tabBg` / `@tabActiveBg` / `@tabHoverBg`，底部和右侧面板使用顶部边框高亮（`border-top: 2px solid @accent`）。
- **表格**：`QTableView` / `QTableWidget` 启用交替行背景 `@altRowBg`，选中态 `@selectionBg`。
- **分割器**：`QSplitter::handle` 透明，悬停时显示 `@accent` 细线。
- **滚动条**：宽度 8–10px，圆角 4–5px，跟随主题。
- **按钮**：统一 `QPushButton` 基础样式，禁用态使用 `@buttonDisabledBg` / `@buttonDisabledText`。
- **状态栏**：24px 高，背景 `@statusBg`，前景 `@statusFg`。

### 3.4 原型与运行时分离
`UI/` 下的 HTML/CSS 仅用于浏览器原型验证，不参与 Qt 构建；Qt 运行时完全依赖 QSS。两者共享相同的布局思想（菜单栏 + 左侧 ActivityBar + 中央 Tab + 右侧 Dock + 底部 Dock + 状态栏），但技术栈不同。

## 4. 约定与约束

- **所有颜色必须通过 `@变量` 引用**：`theme.qss` 模板中禁止直接写死十六进制颜色值，新增 token 需同时在 `ThemeManager::initThemes` 的每个主题中补齐，并在 `generateQss` 的 `varMap` 中注册映射。
- **主题扩展方式**：新增主题只需在 `ThemeManager::initThemes` 中添加一个 `Theme` 结构体，无需修改任何 QSS。
- **开发期热重载**：修改 `styles/theme.qss` 后重启应用即可生效，无需重新编译；生产环境自动回退到 qrc 中的副本。
- **ID 选择器命名规范**：组件标题统一使用 `#CollapsibleTitle`、`#DockPanelTitle`、`#SidePanelTitle`、`#DbcDetailTitle` 等固定 ID，确保跨主题一致性。
- **字体**：全局使用 `Segoe UI` / `Microsoft YaHei UI` / sans-serif 字体栈，代码区域使用 `Consolas` / monospace。
- **原型 CSS 独立演进**：`UI/css/ui-prototype.css` 不受 QSS 约束，可自由使用现代 CSS（如 `display:contents`、CSS Grid、Flexbox），但应保持与 Qt 界面一致的布局语义以便对照。
- **无第三方 UI 框架**：项目未引入 Element、Ant Design、Tailwind 等前端库；纯手工 QSS + 少量原生 CSS 原型。
- **图标资源**：通过 `resources/icons/*.svg` 以 SVG 形式管理，由 C++ 侧加载，不在样式文件中内联。