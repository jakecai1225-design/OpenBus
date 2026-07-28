---
kind: frontend_style
name: Qt QSS 样式系统（VS Code 风格主题）
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - resources/resources.qrc
    - src/main.cpp
---

本项目使用 Qt 的 QSS（Qt Style Sheets）作为唯一的 UI 样式方案，整体视觉风格模仿 VS Code 的暗色/浅色混合主题。

**样式系统与工具**
- 样式语言：QSS（类 CSS 语法），通过 `QApplication::setStyleSheet()` 全局加载。
- 资源管理：`resources.qrc` 将 `styles/default.qss` 打包进 Qt 资源系统，启动时在 `main.cpp` 中通过 `:/styles/default.qss` 路径读取并应用。
- 无第三方 UI 框架或 CSS-in-JS 方案，纯 Qt Widgets + QSS。

**核心文件与位置**
- `resources/styles/default.qss`：全部样式定义（约 560 行），覆盖 QMainWindow、菜单栏、DockWidget、工具栏、表格、滚动条、状态栏、自定义控件 ID 等。
- `resources/resources.qrc`：Qt 资源清单，声明 `styles/default.qss` 的路径前缀为 `/`。
- `src/main.cpp`：程序入口，在创建 `QApplication` 后加载 QSS 并应用到整个应用。

**架构与约定**
- 单一主题文件：所有视觉样式集中在一个 `.qss` 文件中，未拆分为多主题或动态切换机制。
- 命名约定：通过 `#IdName` 选择器对自定义控件进行样式绑定（如 `#ActivityBar`、`#SidePanelTitle`、`#ProjectList`、`#BottomPanel`、`#RightPanel`、`#WindowButtons`、`#WinCloseBtn` 等），这些 ID 需在对应 C++ 组件中通过 `setObjectName()` 设置。
- 颜色体系：主色调为 `#4a90d9`（蓝色高亮），背景以 `#f0f0f0` / `#e0e0e0` / `#2D2D2D` 为主，文本颜色遵循 VS Code 风格（浅灰 `#e0e0e0`、深灰 `#333`、选中蓝 `#c5d9f1`）。
- 字体：全局 `font-size: 12px`，代码编辑区使用等宽字体 `Consolas, "Courier New"`。

**运行时样式覆盖**
- 部分组件在 C++ 代码中通过 `setStyleSheet()` 进行局部覆盖，属于内联样式覆盖策略：
  - `bottompanel.cpp`：提示标签设置为等宽粗体蓝色。
  - `filterbar.cpp`：根据过滤结果动态设置输入框背景色（绿色/红色）。
  - `panels/sidebarpanels.cpp`：状态标签颜色在 green/gray 之间切换。
- 这些内联样式仅用于状态反馈，不影响全局主题。

**约束与规范**
- 样式必须通过 `default.qss` 集中管理，避免散落的硬编码样式（除必要的状态反馈外）。
- 自定义控件必须设置 `objectName` 以便通过 `#Id` 选择器匹配样式。
- 未实现多主题切换、深色/浅色模式切换或用户自定义样式能力。