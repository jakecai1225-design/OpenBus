---
kind: frontend_style
name: Qt 主题系统与 CSS 原型双轨样式体系
category: frontend_style
scope:
    - '**'
source_files:
    - UI/css/ui-prototype.css
    - resources/styles/default.qss
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - UI/ui-layout.md
---

本项目的 UI 样式采用「CSS 原型 + Qt QSS 主题」双轨并行架构：前端原型使用纯 HTML/CSS/JS 在浏览器中快速验证布局与交互，桌面端通过 Qt6 的 QSS（Qt Style Sheet）实现运行时主题切换。两套样式相互独立但视觉规范保持一致。

### 1. 样式系统与技术栈
- **原型层**：`UI/css/ui-prototype.css` 提供完整的 VS Code 风格深色原型样式，包含菜单栏、左右 Dock、中央标签页、底部终端、状态栏等全部区域；`UI/js/ui-prototype.js` 驱动原型交互。
- **运行时层**：`resources/styles/default.qss` 是默认浅色主题 QSS，覆盖 QMainWindow、QMenuBar、QDockWidget、QTabBar、QTableView、QScrollBar 等所有 Qt 控件。
- **主题引擎**：`src/ui/thememanager.cpp/.h` 实现 ThemeManager 单例，内置 7 套主题（Light、Dark、VS Code Dark+、VS Code Light+、Monokai、Solarized Light、Solarized Dark），通过 `qApp->setStyleSheet()` 动态应用。

### 2. 核心文件与职责
- `UI/css/ui-prototype.css` — 原型样式，定义布局结构、颜色变量、组件样式
- `resources/styles/default.qss` — 默认 QSS 主题，按 Qt 控件命名空间组织
- `src/ui/thememanager.cpp` — 主题数据（Theme 结构体含 39 个颜色字段）+ QSS 生成器
- `UI/ui-layout.md` — UI 布局权威描述文档，定义设计规范、色彩规范、字体规范
- `UI/ui-prototype.html` + `UI/partials/*.html` — 原型 HTML 骨架，按 partials 拆分模块

### 3. 架构与设计决策
- **设计先行**：所有 UI 变更必须先修改 `ui-layout.md`，经浏览器原型确认后再实现 Qt 代码，确保设计与实现一致。
- **主题即数据**：Theme 结构体将颜色拆分为 windowBg、contentBg、sidebarBg、panelBg、barBg、accent、selectionBg 等语义化字段，generateQss() 用 QString::arg() 拼接完整 QSS。
- **双态渲染**：原型用 CSS class（如 `.scenario-bar`、`.center-tabs`、`.trace-table`），Qt 用 QSS selector（如 `QTabBar::tab`、`#ActivityBar QToolButton`），两者通过 ui-layout.md 中的色值约定对齐。
- **分区样式策略**：
  - 菜单栏/活动栏统一深色背景（`#2D2D2D` / `#1e1e1e`）
  - 侧边栏/面板标题用中性灰（`#e0e0e0` / `#2d2d2d`）
  - 内容区浅色主题用白/浅灰，深色主题用深灰
  - 强调色统一为蓝色系（`#4a90d9` / `#0e639c` / `#007acc`）

### 4. 约束与约定
- **色彩一致性原则**（来自 ui-layout.md 第 8.2 节）：所有区域（MenuBar、ActivityBar、SideBar、CenterArea、BottomDock、RightDock、StatusBar、Dialog、ScrollBar）的颜色必须由当前主题统一控制，不允许出现与主题不一致的配色。
- **主题覆盖范围**：ThemeManager 必须覆盖所有 Qt 原生控件样式，包括 QMenu、QDockWidget、QTreeWidget、QListWidget、QLineEdit、QPushButton、QTableView、QHeaderView、QSplitter、QSlider、QComboBox、QCheckBox、QGroupBox、QDialog、QPlainTextEdit、QTableWidget、QScrollBar 等。
- **字体规范**：全局 12px 系统字体，TraceView/FrameInfo/SignalDecode 等表格区域强制使用 Consolas 等宽字体 10pt。
- **窗口行为**：无边框窗口（Qt::FramelessWindowHint），菜单栏兼作标题栏支持拖拽移动和双击最大化。
- **原型到实现的映射**：CSS class 名（如 `.activitybar`、`.center-tabs`、`.bottom-dock`）与 Qt 对象名（`#ActivityBar`、`#BottomPanel`、`#RightPanel`）需保持语义对应关系。