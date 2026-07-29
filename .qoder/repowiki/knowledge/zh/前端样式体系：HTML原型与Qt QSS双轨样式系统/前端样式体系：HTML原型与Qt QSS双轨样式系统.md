---
kind: frontend_style
name: 前端样式体系：HTML原型与Qt QSS双轨样式系统
category: frontend_style
scope:
    - '**'
source_files:
    - UI/css/ui-prototype.css
    - UI/ui-layout.md
    - resources/styles/default.qss
    - UI/ui-prototype.html
    - UI/js/ui-loader.js
    - UI/js/ui-prototype.js
    - resources/resources.qrc
---

## 1. 系统与工具
- 前端原型：纯 HTML + CSS + JavaScript，位于 `UI/` 目录，通过 `ui-prototype.html` 作为可交互原型入口。
- Qt 运行时样式：使用 Qt Style Sheets（QSS），位于 `resources/styles/default.qss`，以 VS Code 深色风格为基准主题。
- 构建集成：Python 脚本 `scripts/build.py` 负责打包 Qt 资源（含 QSS），CMake 管理 C++ 源码与 Qt 资源编译。

## 2. 关键文件与包
- `UI/css/ui-prototype.css`：原型界面全部样式，定义菜单栏、三栏 Dock、标签页、表格、对话框、右键菜单等。
- `UI/js/ui-loader.js` / `UI/js/ui-prototype.js`：原型交互逻辑（部分加载、菜单、标签页切换等）。
- `UI/partials/*.html`：按区域拆分的 HTML 片段（菜单栏、左/右/底部 Dock、中央区域、对话框、覆盖层等）。
- `UI/ui-layout.md`：UI 布局唯一权威描述，规定窗口结构、面板行为、色彩规范、字体规范及变更记录。
- `resources/styles/default.qss`：Qt 运行时 QSS 样式表，覆盖 QMainWindow/QMenuBar/QDockWidget/QTabWidget/QTableView/QScrollBar 等所有原生控件。
- `resources/resources.qrc`：Qt 资源清单，将 QSS 等资源纳入构建。

## 3. 架构与设计约定
- “原型先行”工作流：所有 UI 变更必须先修改 `ui-layout.md`，在浏览器中通过 `ui-prototype.html` 确认后再实现 Qt 代码。该文档是 Qt 实现的唯一权威来源。
- 双轨样式：
  - 原型阶段用 CSS 快速验证布局与交互；
  - 生产阶段由 QSS 驱动 Qt 控件外观，两者颜色、字号、边框等保持一致（如强调色 `#4a90d9`、背景 `#2D2D2D`、选中高亮 `#c5d9f1` 等）。
- 模块化 HTML：通过 partials 拆分菜单栏、三栏 Dock、中央标签页、对话框、覆盖层等，便于独立维护与动态加载。
- 主题系统：内置 7 套主题（Light/Dark/VS Code Dark+/VS Code Light+/Monokai/Solarized Light/Solarized Dark），通过配置面板切换，所有区域（菜单栏、侧边栏、编辑区、状态栏、滚动条、对话框）颜色由主题统一派生，禁止出现与主题不一致的配色。
- 响应式策略：原型基于 Flexbox 布局（`.window`、`.main-body`、`.split-container` 等），Qt 端通过 `QSplitter`、`QDockWidget` 实现可拖拽调整大小的 Dock 面板。

## 4. 约定与约束
- 布局约束（来自 `ui-layout.md`）：
  - 无边框窗口（`Qt::FramelessWindowHint`），菜单栏兼标题栏，支持拖拽移动与双击最大化。
  - 左侧栏（ActivityBar + SideBar）永久停靠，宽度默认 300px；右侧栏默认 260px；底部栏默认高度 180px。
  - 中央编辑区支持标签页多开、拆分（水平/垂直）、拖拽分离为独立窗口后合并。
  - 所有 Dock 面板边界线可拖拽调整大小，支持关闭、最大化、最小化与一键重置布局。
- 色彩与字体约束（来自 `ui-layout.md` 第 8-9 节）：
  - 全局字体 12px，TraceView/FrameInfo/SignalDecode 使用等宽字体 Consolas 10pt。
  - 强调色统一为 `#4a90d9`，选中高亮 `#c5d9f1`，背景色系遵循所选主题。
  - 所有区域（MenuBar、ActivityBar、SideBar、CenterArea、BottomDock、RightDock、StatusBar、Dialog、ScrollBar）必须与当前主题一致。
- 组件样式约定（来自 `default.qss`）：
  - 菜单栏背景 `#2D2D2D`，文字 `#e0e0e0`，选中项背景 `#3D3D3D`，按下项背景 `#4a90d9`。
  - TabBar 选中项底部 2px 实线 `#4a90d9`，未选中 hover 背景 `#d8d8d8`。
  - QTableView 交替行背景 `#f7f7f7`，选中背景 `#c5d9f1`，网格线 `#e8e8e8`。
  - QScrollBar 圆角 5px，hover 变深灰 `#a0a0a0`。
  - 终端面板 `#TerminalOutput` 保持深色背景 `#1e1e1e`，与其他浅色文本编辑区分开。
- 原型与 Qt 一致性：CSS 类名与 QSS 选择器虽不同，但视觉表现（颜色、字号、边框、间距）需严格对齐，确保原型与最终产品外观一致。

## 5. 适用性与范围
- 本仓库同时包含“前端原型”（HTML/CSS/JS）与“Qt 桌面应用”（C++/QSS）两套样式实现，前者用于设计验证，后者用于生产运行。
- 样式体系围绕 CAN/CAN FD 分析工具的专业场景设计，强调数据表格、波形图、DBC 树形结构的清晰可读性，以及深色主题的长时间使用友好性。