---
kind: frontend_style
name: Qt QSS 主题系统与 HTML 原型样式体系
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - src/ui/thememanager.h
    - UI/css/ui-prototype.css
    - UI/ui-prototype.html
    - UI/partials/menubar.html
    - UI/partials/left-dock.html
    - UI/partials/center-area.html
    - UI/partials/right-dock.html
    - UI/partials/bottom-dock.html
---

本项目的 UI 样式采用 **双轨并行** 的架构：运行时由 Qt6 原生 QSS（Qt Style Sheets）驱动，设计/交互原型由独立 HTML+CSS 实现。

### 1. 系统与方法论
- **QSS 主题引擎**：通过 `src/ui/thememanager.h` 中的 `ThemeManager` 单例管理多套主题。`Theme` 结构体定义了 40+ 个颜色字段（windowBg、contentBg、sidebarBg、accent、selectionBg、terminalBg 等），`generateQss()` 方法根据主题数据动态生成完整 QSS 字符串并应用到 QApplication。
- **默认主题文件**：`resources/styles/default.qss` 是完整的 VS Code 风格浅色主题，覆盖 QMainWindow、QMenuBar、QDockWidget、QTabBar、QTableView、QScrollBar、QStatusBar、QSplitter 等全部常用控件，使用硬编码十六进制色值（如 `#4a90d9` 强调色、`#2D2D2D` 深色栏背景）。
- **HTML 原型样式**：`UI/css/ui-prototype.css`（243 行）与 `UI/ui-prototype.html` + `partials/*.html` 构成独立的 Web 原型，用于 UI 布局验证和交互演示，不随构建产物打包。

### 2. 关键文件与包
- `resources/styles/default.qss` — 主运行主题，598 行完整 QSS
- `src/ui/thememanager.h` — 主题数据结构与生成器接口
- `UI/css/ui-prototype.css` — 原型 CSS，定义场景切换器、菜单栏、左侧 Dock、中央标签页、Trace 表格、图形视图、DBC 详情页、右侧属性面板、底部终端、状态栏、对话框、右键菜单等模块样式
- `UI/ui-prototype.html` — 原型入口，通过 `ui-loader.js` 动态加载 partials 片段
- `UI/partials/*.html` — 模块化 HTML 片段（menubar、left-dock、center-area、right-dock、bottom-dock、dialogs、overlays、scenario-bar）

### 3. 架构与约定
- **主题即数据**：新增主题只需在 ThemeManager 中注册一个 `Theme` 实例，无需手写 QSS，`generateQss()` 自动填充模板。
- **命名空间隔离**：QSS 大量使用 `#ActivityBar`、`#SidePanelTitle`、`#BottomPanel`、`#DbcDetailTitle` 等 ID 选择器精确控制特定组件；CSS 原型使用 `.activitybar`、`.center-tabs`、`.trace-table`、`.dock-pane` 等类名。
- **VS Code 视觉语言**：深色菜单栏（`#2D2D2D`）、浅灰内容区（`#f5f5f5`/#`#ffffff`）、蓝色强调（`#4a90d9`）、圆角按钮（`border-radius: 3px`）、细边框分隔（`#c0c0c0`）贯穿两套样式。
- **组件化布局**：原型将界面拆分为 left-dock / center-area / right-dock / bottom-dock 四个区域，通过 Flexbox 组合，与 Qt 侧的 ActivityBar + SidePanel + TabWidget + DockWidget 布局一一对应。

### 4. 约定与约束
- QSS 中所有颜色以十六进制硬编码，未引入 CSS 变量或外部调色板；主题切换依赖 `ThemeManager::applyTheme()` 重新生成并设置全局 stylesheet。
- 部分 Qt 组件仍使用 `setStyleSheet("...")` 内联样式（如 `canopenview.cpp`、`dbcdetailtab.cpp`、`deviceconnectiontab.cpp` 中的状态标签），属于局部覆盖，未被统一主题完全接管。
- 原型 CSS 与运行时 QSS 保持语义对齐但语法独立，二者不共享变量，修改时需同步更新两处。
- 滚动条、分割线、选中态等细节在 QSS 中显式定制（如 `QScrollBar::handle:vertical`、`QSplitter::handle:hover` 高亮为 `#4a90d9`），确保跨平台一致性。