---
kind: frontend_style
name: 基于 Qt QSS + ThemeManager 的 VS Code 风格主题系统
category: frontend_style
scope:
    - '**'
source_files:
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - resources/styles/theme.qss
    - styles/theme.qss
    - resources/styles/default.qss
---

## 1. 采用的样式体系

本项目使用 **Qt StyleSheet (QSS)** 作为唯一的前端样式方案，没有引入 CSS/SCSS、Tailwind 或任何第三方 UI 框架。整体视觉风格明确为 **VS Code 风格**（扁平、简洁、统一），通过 `resources/styles/default.qss` 提供一套硬编码的浅色默认主题，并通过 `ThemeManager` 在运行时用 `@变量` 模板替换实现多主题切换。

## 2. 关键文件与包

- `src/ui/thememanager.h` / `src/ui/thememanager.cpp`：主题管理核心。定义 `Theme` 结构体（包含 windowBg、contentBg、sidebarBg、panelBg、barBg、accent、selectionBg、tabBg、scrollHandle 等 30+ 设计 token），内置 Light、Dark、VS Code Dark+、VS Code Light+、Monokai、Solarized Light/Dark 共 6 套主题配色，并提供 `applyTheme()` / `generateQss()`。
- `resources/styles/theme.qss`：QSS 模板文件，所有颜色值以 `@windowBg`、`@text`、`@accent`、`@borderDim` 等占位符书写；注释明确“修样式只需改本文件 + 重启，无需重新编译”。
- `styles/theme.qss`：与 `resources/styles/theme.qss` 内容完全一致的副本，供开发模式从文件系统加载（优先于 qrc 资源）。
- `resources/styles/default.qss`：硬编码的浅色默认主题，直接给每个控件写死颜色值，作为未启用主题切换时的兜底样式。
- `src/main.cpp` 中通过 `ThemeManager::instance()->applyTheme(...)` 应用主题。

## 3. 架构与设计约定

### 3.1 设计 Token 化
`Theme` 结构体将颜色抽象为语义化 token，覆盖背景（windowBg/contentBg/sidebarBg/panelBg）、文字（text/textDim）、强调色（accent/accentHover/accentBorder）、边框（border/borderDim）、选择态（selectionBg/hoverBg）、按钮态（buttonBg/buttonHover/buttonPress/buttonDisabledBg/buttonDisabledText）、状态栏（statusBg/statusFg）、终端（terminalBg/terminalFg）、标签页（tabBg/tabActiveBg/tabHoverBg）、滚动条（scrollBg/scrollHandle/scrollHandleHover）、关闭按钮（closeBtnHover/closeBtnPress）、表头（headerBg/headerHover）、交替行（altRowBg）等。新增主题只需填充这些字段，无需修改 QSS。

### 3.2 运行时主题替换机制
`generateQss()` 通过 `QHash<QString, QString Theme::*>` 将 `@变量名` 映射到 `Theme` 成员指针，遍历替换生成最终 QSS 字符串并调用 `qApp->setStyleSheet()`。加载顺序：优先读取当前工作目录下的 `styles/theme.qss`（开发时热更新），回退到 qrc 中的 `:styles/theme.qss`。

### 3.3 组件级样式约定
- 面板标题统一使用 `#CollapsibleTitle`、`#DockPanelTitle`、`#SidePanelTitle`、`#DbcDetailTitle` 四个 ID 选择器，高度固定 30px，背景 `@panelBg`，底部 `@borderDim` 分隔线。
- 侧边栏通用控件使用 `#SidePanelSubTitle`、`#SidePanelButton`、`#SidePanelHint`、`#SidePanelInfo`、`#DimLabel`、`#SectionLabel`、`#StatusDim`、`#StatusWarn`、`#StatusOk`、`#StatusRec`、`#StatusSuccess`、`#StatusError`、`#NmtStatus` 等 ID 选择器，保持统一的字号（11–14px）和间距。
- ActivityBar 始终深色（即使浅色主题下也使用 `activityBarBg`），选中态通过左侧 2–3px 的 `@accent` 竖线标识。
- 标签页主区域使用 `QTabBar::tab` 配合 `border-bottom` 高亮，底部/右侧面板标签使用 `border-top` 高亮，形成区分。
- 表格统一使用 `alternate-background-color: @altRowBg` 实现斑马纹。
- 分割器 handle 透明，悬停显示 `@accent` 细线（1px）。
- 滚动条宽度 8–10px，圆角 4–5px，无箭头按钮。
- 窗口控制按钮（最小化/最大化/关闭）通过 `#WindowButtons`、`#WinMinBtn`、`#WinMaxBtn`、`#WinCloseBtn` 自定义，关闭按钮悬停变红（`closeBtnHover`）。

### 3.4 与 SignalSendTab / DbcImportDialog 的关系
这两个组件属于 `src/ui/` 下的 Qt 原生 C++ 控件，本身不内联样式，而是依赖全局 QSS 生效。它们的布局由代码构造，外观由上述 QSS 规则统一约束（如 QPushButton、QLineEdit、QTableView、QTabWidget 等）。`DbcDetailTab` 相关的 `#DbcDetailPageTitle`、`#DbcDetailInfo`、`#DbcDetailComment`、`#DbcDetailPlaceholder`、`#DbcDetailTitle` 等 ID 选择器专门为其定制样式。

## 4. 约定与约束

- **禁止在组件代码中硬编码颜色**：所有颜色必须通过 QSS 的 `@变量` 或 `default.qss` 中的选择器声明，新增主题只需修改 `ThemeManager::initThemes()` 中的 `Theme` 实例。
- **QSS 模板位于 `styles/theme.qss` 或 `resources/styles/theme.qss`**，不得在其他位置散落样式定义。
- **面板标题统一使用 30px 高度**（见 QSS 中 `min-height: 30px` 的注释约定）。
- **ActivityBar 宽度固定 50px**（QSS 注释标注）。
- **标签页高度约定**：主区域 36px、底部面板 28px、右侧面板 28px（QSS 注释中明确）。
- **字体统一**：全局使用 `Segoe UI` / `Microsoft YaHei UI` 无衬线字体，文本编辑区使用 `Consolas` / `Courier New` 等宽字体。
- **主题切换即时生效**：通过 `ThemeManager::applyTheme()` 调用 `qApp->setStyleSheet()` 全局刷新，无需重建窗口。
- **开发模式热更新**：运行期优先加载磁盘 `styles/theme.qss`，修改后重启即可看到效果，无需重新编译。
- **默认主题兜底**：若模板文件缺失，回退到 qrc 中的 `theme.qss`；若仍为空则返回空字符串，此时 `default.qss` 作为最终兜底样式生效。

## 5. 总结

该项目采用纯 Qt QSS + 自研 `ThemeManager` 的主题系统，通过设计 token 化 + 运行时 `@变量` 替换实现了多主题（Light/Dark/Monokai/Solarized/VS Code）无缝切换，整体遵循 VS Code 风格的扁平化设计规范，所有 UI 组件（包括 SignalSendTab 与 DbcImportDialog）均通过统一的 QSS 选择器获得一致的外观。