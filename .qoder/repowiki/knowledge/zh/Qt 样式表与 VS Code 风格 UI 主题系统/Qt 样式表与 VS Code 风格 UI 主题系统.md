---
kind: frontend_style
name: Qt 样式表与 VS Code 风格 UI 主题系统
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - UI/ui-prototype.html
    - UI/ui-layout.md
    - src/ui/mainwindow.cpp
---

## 1. 样式系统与工具
- 使用 Qt6 的 QSS（Qt Style Sheets）作为唯一样式机制，通过 `QApplication::setStyleSheet()` 加载全局样式。
- 样式文件集中存放在 `resources/styles/default.qss`，由 Qt 资源系统（`resources/resources.qrc`）打包进应用。
- 无 CSS/SCSS/Tailwind 等前端样式语言，所有视觉表现均通过 QSS 选择器控制 Qt 原生控件外观。

## 2. 核心样式文件与资源
- `resources/styles/default.qss`：完整的主题样式表，覆盖菜单栏、DockWidget、表格、按钮、滚动条、状态栏、文本编辑器等全部控件。
- `resources/resources.qrc`：Qt 资源清单，将样式表和其他资源编译进二进制。
- `UI/ui-prototype.html`：基于 HTML/CSS 的高保真交互原型，用于在浏览器中预览和验证 UI 布局后再实现到 Qt 代码。
- `UI/ui-layout.md`：UI 布局描述规范文档，定义窗口结构、颜色规范、字体规范、各面板布局及交互流程，是 UI 变更的唯一权威来源。

## 3. 架构与设计约定
- **VS Code 风格无边框窗口**：主窗口使用 `Qt::FramelessWindowHint`，菜单栏兼作标题栏，支持拖拽移动和双击最大化。
- **三栏式布局**：左侧 ActivityBar + SideBar（永久停靠）、中央 SplitEditorArea（可拆分标签页）、右侧 RightDock（AI对话/快捷按钮）、底部 BottomDock（终端/输出/问题）、顶部 StatusBar。
- **设计原则**：左侧栏只做入口和列表，详细信息放在中央标签页内部；每个功能模块拥有独立标签页，支持多开。
- **色彩体系**：深色菜单栏（#2D2D2D）、浅色侧边栏（#f5f5f5）、白色工作区、蓝色强调色（#4a90d9）、选中高亮（#c5d9f1）。
- **字体体系**：全局 12px 系统字体，表格和编辑区域使用 Consolas 等宽字体 10pt。

## 4. 样式组织与约束
- **单一主题源**：所有样式集中在 `default.qss`，通过 ID 选择器（如 `#ActivityBar`、`#SidePanelTitle`、`#BottomPanel`）精确控制特定组件。
- **命名约定**：关键 UI 组件设置固定 objectName（如 ActivityBar、SidePanelTitle、DockPanelTitle、ProjectList、DbcDetailTitle），便于 QSS 精准匹配。
- **控件样式覆盖**：对 QTableView、QPlainTextEdit、QScrollBar、QTabBar、QToolButton 等 Qt 控件进行统一外观定制，确保一致的 VS Code 风格。
- **暗色编辑区**：QPlainTextEdit 使用深黑背景（#1e1e1e）配浅灰文字（#d4d4d4），模拟代码编辑器体验。
- **状态栏蓝色主题**：StatusBar 使用 #4a90d9 背景配白色文字，形成品牌识别色。
- **原型驱动开发**：任何 UI 变更必须先修改 `ui-layout.md`，在 `ui-prototype.html` 中验证后，再实现到 Qt 代码，保证设计与实现一致。

## 5. 运行时样式加载
- 主窗口在构造时创建菜单栏、窗口按钮、布局和状态栏。
- 样式表通过 Qt 资源系统加载并应用到整个应用程序，确保所有子控件继承统一主题。
- 自定义控件（如 ActivityBar、TraceView、GraphicView）通过 objectName 与 QSS 选择器关联，获得一致的视觉风格。