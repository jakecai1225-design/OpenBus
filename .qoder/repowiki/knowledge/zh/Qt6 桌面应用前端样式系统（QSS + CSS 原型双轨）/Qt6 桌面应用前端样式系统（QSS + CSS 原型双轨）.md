---
kind: frontend_style
name: Qt6 桌面应用前端样式系统（QSS + CSS 原型双轨）
category: frontend_style
scope:
    - '**'
source_files:
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - resources/styles/default.qss
    - resources/resources.qrc
    - UI/css/ui-prototype.css
    - UI/js/ui-loader.js
    - UI/js/ui-prototype.js
    - UI/ui-layout.md
---

本仓库采用「Qt QSS 主题引擎 + Web 原型 CSS」双轨样式体系，服务于同一套 UI 布局：运行时通过 Qt 的 StyleSheet（QSS）驱动真实界面，开发期通过 HTML/CSS/JS 原型快速验证交互与布局。

### 1. 使用的系统与工具
- **Qt StyleSheet (QSS)**：作为运行时样式核心，覆盖 QMainWindow、QMenuBar、QTabBar、QTableView、QScrollBar、QPushButton 等全部 Qt 控件族，实现 VS Code 风格的深色/浅色主题。
- **CSS 原型**：`UI/css/ui-prototype.css` 提供与 QSS 一一对应的浅色/深色视觉原型，配合 `UI/js/ui-prototype.js` 和多个 `partials/*.html` 片段在浏览器中预览布局。
- **ThemeManager 主题引擎**：C++ 单例 `ThemeManager` 集中管理多套主题色板（Light、Dark、VS Code Dark+/Light+、Monokai、Solarized Light/Dark），通过 `generateQss()` 将 `Theme` 结构体渲染为完整 QSS 字符串并 `qApp->setStyleSheet()` 全局生效。
- **资源打包**：默认 QSS 文件 `resources/styles/default.qss` 通过 `resources/resources.qrc` 编译进应用，作为初始样式。

### 2. 关键文件与位置
- `src/ui/thememanager.h` / `src/ui/thememanager.cpp` — 主题数据结构与 QSS 生成器
- `resources/styles/default.qss` — 默认静态 QSS（Light 主题）
- `resources/resources.qrc` — Qt 资源清单，注册样式文件
- `UI/css/ui-prototype.css` — 原型 CSS（与 QSS 语义对齐）
- `UI/js/ui-loader.js` / `UI/js/ui-prototype.js` — 原型加载与交互脚本
- `UI/partials/*.html` — 分片 HTML（menubar、left-dock、center-area、right-dock、bottom-dock、dialogs、overlays、scenario-bar）
- `UI/ui-layout.md` — 布局说明文档
- `src/ui/mainwindow.*` — 主窗口集成 ThemeManager 与 QSS 切换入口

### 3. 架构与设计约定
- **主题即数据**：`Theme` 结构体定义 40+ 个颜色字段（windowBg、contentBg、sidebarBg、panelBg、barBg、accent、selectionBg、tabBg、scrollHandle 等），每套主题仅填充这些字段，QSS 模板通过 `.arg()` 注入，保证样式与配色解耦。
- **双轨一致性**：CSS 原型类名（如 `.menubar`、`.activitybar`、`.trace-table`、`.split-container`）与 QSS 选择器（`QMenuBar`、`#ActivityBar`、`QTableView`、`QSplitter::handle`）按功能域一一对应，便于原型与运行时视觉对齐。
- **区域化样式**：左侧 ActivityBar/SideBar、中央 TabPane、右侧 Dock、底部 Dock 各自有独立背景与边框约定，状态栏统一使用 accent 色（默认 `#4a90d9`）。
- **终端区例外**：Terminal 输出区强制深色背景（`#1e1e1e`）与亮色文字，不跟随通用内容区主题。
- **字体与字号**：全局 `font-size: 12px`，代码/表格列使用 `Consolas, "Courier New", monospace`；中文回退 `Microsoft YaHei`。

### 4. 约定与约束
- **新增主题必须补齐 Theme 所有字段**：`initThemes()` 中每套主题需完整赋值，否则 `generateQss()` 会因缺失参数导致 QSS 异常。
- **QSS 优先级**：`default.qss` 作为基线样式，运行时 `ThemeManager::applyTheme()` 生成的 QSS 会覆盖同名规则，因此新增主题只需关注颜色差异。
- **原型与运行时同步**：修改 `ui-prototype.css` 后需在浏览器中验证，再对应更新 `thememanager.cpp` 中的 QSS 模板或 `default.qss`，保持视觉一致。
- **组件 ID 命名**：QSS 大量依赖 `#ActivityBar`、`#ProjectList`、`#DockPanelTitle`、`#TerminalOutput` 等固定 objectName，新建控件需遵循该命名规范以便被样式命中。
- **滚动条与分割器**：垂直/水平滚动条 handle 圆角统一为 `5px`，分割器 hover 时高亮为 accent 色，确保可发现性。
- **对话框与菜单**：菜单项选中态统一使用 accent 色背景 + 白色文字；关闭按钮 hover/pressed 分别映射到 `closeBtnHover` / `closeBtnPress` 字段。