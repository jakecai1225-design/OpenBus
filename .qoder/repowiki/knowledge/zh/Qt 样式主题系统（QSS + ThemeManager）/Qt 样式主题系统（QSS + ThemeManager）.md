---
kind: frontend_style
name: Qt 样式主题系统（QSS + ThemeManager）
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - UI/css/ui-prototype.css
---

本项目的 UI 样式采用 **双轨制**：原型界面使用独立 HTML/CSS，正式 Qt 桌面端通过 QSS（Qt Style Sheets）+ ThemeManager 实现可切换的主题系统。

### 1. 样式系统与工具
- **Qt Style Sheets (QSS)**：所有 Qt 控件的视觉外观由 QSS 文件统一控制，默认样式位于 `resources/styles/default.qss`，覆盖菜单栏、DockWidget、TabBar、表格、滚动条、按钮、输入框等全部原生控件。
- **ThemeManager**：C++ 单例类（`src/ui/thememanager.cpp/.h`），集中管理多套主题色板，运行时通过 `qApp->setStyleSheet()` 动态切换。
- **HTML/CSS 原型**：`UI/css/ui-prototype.css` 为 Web 原型样式，与 Qt 样式相互独立，用于快速验证布局与交互。

### 2. 核心文件与包
- `resources/styles/default.qss` — 完整 QSS 样式表，定义全局字体、颜色、边框、间距等。
- `src/ui/thememanager.h` / `src/ui/thememanager.cpp` — 主题数据结构 `Theme` 及主题管理器，内置 Light、Dark、VS Code Dark+/Light+、Monokai、Solarized Light/Dark 共 7 套主题。
- `UI/css/ui-prototype.css` — 原型 CSS，包含暗色背景 `#1a1a1a`、强调色 `#4a90d9`、面板/表格/对话框等样式。
- `resources/resources.qrc` — Qt 资源文件，打包 QSS 等资源。

### 3. 架构与约定
- **主题数据模型**：`Theme` 结构体将颜色拆分为 39 个语义化字段（windowBg、contentBg、sidebarBg、panelBg、barBg、accent、selectionBg、buttonBg、statusBg、terminalBg、tabBg、scrollBg、headerBg、altRowBg 等），按功能域分组，便于跨组件一致性。
- **QSS 生成策略**：`generateQss()` 使用 QString 模板 + `.arg()` 占位符拼接，将 Theme 字段映射到 QSS 规则中，避免硬编码颜色值。
- **主题注册机制**：`initThemes()` 在构造时预定义所有主题，`applyTheme(name)` 查找匹配项并调用 `qApp->setStyleSheet(generateQss(theme))` 全局生效。
- **命名空间约定**：QSS 中使用 `#ObjectId`（如 `#ActivityBar`、`#ProjectList`、`#BottomPanel`、`#RightPanel`、`#TerminalOutput`）精确选择特定 Widget；CSS 原型使用 class 选择器（`.scenario-bar`、`.menubar`、`.trace-table` 等）。
- **深色终端隔离**：终端区域通过 `#TerminalOutput` 强制使用深色背景（`#1e1e1e`），与其他浅色内容区分离。

### 4. 约定与约束
- **颜色必须通过 Theme 字段引用**：所有 QSS 中的颜色值不得硬编码，必须经由 `ThemeManager::generateQss()` 的占位符注入，确保主题切换一致生效。
- **主题字段完整性**：新增 UI 元素若涉及背景、前景、边框、悬停、选中态等状态，需在 `Theme` 结构体中补充对应字段并在 `generateQss()` 中映射。
- **QSS 优先级**：`default.qss` 作为基础样式加载，ThemeManager 生成的 QSS 会覆盖其颜色值，形成“静态骨架 + 动态配色”的分层。
- **原型与正式样式分离**：`UI/` 下的 HTML/CSS 仅用于原型演示，不进入 Qt 构建流程；正式 UI 完全依赖 QSS 和 C++ 代码。
- **字体统一**：全局字体大小固定为 `12px`，等宽字体使用 `Consolas, "Courier New", monospace`，中文回退使用 `Microsoft YaHei`。
- **强调色规范**：主强调色默认为 `#4a90d9`（蓝色系），悬停态为 `#5a9ee8`，边框为 `#3a7fc9`，关闭按钮危险色为 `#e81123`。