---
kind: frontend_style
name: 基于 Qt QSS + ThemeManager 的运行时主题系统
category: frontend_style
scope:
    - '**'
source_files:
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - resources/styles/theme.qss
    - styles/theme.qss
    - UI/css/ui-prototype.css
    - src/ui/mainwindow.cpp
---

## 1. 采用的样式体系

本项目是 Qt6 桌面应用，前端样式完全基于 **Qt StyleSheet (QSS)**，没有使用 CSS/HTML 作为主 UI（`UI/` 目录仅包含一个纯 HTML+CSS 的“原型界面” `ui-prototype.html`，用于设计阶段预览，不参与构建）。实际运行时的视觉风格由以下三部分构成：

- **QSS 模板**：`resources/styles/theme.qss`（以及根级 `styles/theme.qss` 开发回退路径），定义所有控件外观。
- **主题色板**：`src/ui/thememanager.h` 中的 `Theme` 结构体，集中声明 30+ 个语义化颜色字段（`windowBg`、`contentBg`、`sidebarBg`、`accent`、`statusBg`、`terminalBg` 等）。
- **主题管理器**：`src/ui/thememanager.cpp` 提供单例 `ThemeManager`，内置 Light / Dark / VS Code Dark+ / VS Code Light+ / Monokai / Solarized Light / Solarized Dark 七套主题，通过 `qApp->setStyleSheet()` 全局生效。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `src/ui/thememanager.h` | 定义 `Theme` 结构体与 `ThemeManager` 接口 |
| `src/ui/thememanager.cpp` | 注册主题、实现 `generateQss()` 变量替换、`applyTheme()` 切换 |
| `resources/styles/theme.qss` | QSS 模板，全部使用 `@变量` 占位符 |
| `styles/theme.qss` | 开发模式下的可热重载副本（优先于 qrc 资源加载） |
| `resources/resources.qrc` | 将 QSS 打包进 qrc 资源 |
| `UI/css/ui-prototype.css` | 原型界面的独立 CSS，与运行时无关 |
| `src/ui/mainwindow.cpp` | 在设置面板主题变更时调用 `ThemeManager::instance()->applyTheme(name)` |

## 3. 架构与设计约定

### 3.1 变量驱动的主题渲染
`ThemeManager::generateQss()` 维护一个从 `@变量名` 到 `Theme` 成员指针的映射表（如 `"@windowBg" → &Theme::windowBg`），读取 QSS 模板后用当前主题的对应颜色值逐字替换。因此新增主题只需：
1. 在 `initThemes()` 中添加一组 `Theme` 实例；
2. 若需新语义色，扩展 `Theme` 结构体并在 `varMap` 中注册；
3. 在 `theme.qss` 中使用对应的 `@变量`。

### 3.2 主题集合与命名
内置主题遵循 IDE/编辑器常见配色方案：Light、Dark、VS Code Dark+、VS Code Light+、Monokai、Solarized Light、Solarized Dark。默认主题为 `Light`。

### 3.3 布局与组件风格约定
- 字体统一为 `Segoe UI, Microsoft YaHei UI, sans-serif`，基础字号 `12px`。
- 侧边栏（ActivityBar 48px 宽）、DockWidget、TabBar、QToolBar 均通过固定 ID（如 `#ActivityBar`、`#LeftDock`、`#BottomPanel`、`#RightPanel`）选择器精确控制。
- 分割器手柄透明、悬停时高亮为 accent 色；滚动条采用 10px 宽度、圆角滑块，模仿 VS Code 风格。
- 状态栏固定 22px 高，背景使用 `statusBg`。
- 表格行交替背景使用 `altRowBg`，选中态使用 `selectionBg`。
- 对话框、QMessageBox 等继承全局窗口背景。

### 3.4 原型与运行态分离
`UI/` 目录（`ui-prototype.html`、`css/ui-prototype.css`、`partials/*.html`、`js/ui-loader.js`、`js/ui-prototype.js`）是一个独立的 HTML/CSS/JS 原型，用于 UI 线框图与交互演示，**不参与 CMake 构建**，也不被 Qt 加载。真正的 Qt 界面在 `src/ui/` 下以 C++ 代码构建。

## 4. 约束与规范

- **禁止硬编码颜色**：QSS 模板中所有颜色必须通过 `@变量` 引用，不得直接写十六进制色值（除少数状态色如 `#StatusWarn`、`#StatusOk` 等辅助类外）。
- **新增主题必须完整覆盖语义字段**：`Theme` 结构体的每个字段都有明确用途（背景、边框、按钮、滚动条、关闭按钮、表头、交替行等），新增主题需填满全部字段以保证一致性。
- **开发期热重载**：`generateQss()` 优先从 `QDir::currentPath() + "/styles/theme.qss"` 读取，不存在才回退到 qrc 资源 `:/styles/theme.qss`，因此修改 `styles/theme.qss` 后重启即可生效，无需重新编译。
- **主题切换入口**：通过 `SettingsDialog` 触发 `SettingsPanel::themeChanged` 信号，由 `MainWindow` 连接至 `ThemeManager::applyTheme()`，用户不可绕过该流程直接修改样式。
- **原型 CSS 与运行 QSS 互不影响**：`UI/css/ui-prototype.css` 使用标准 CSS 语法，而 `theme.qss` 使用 Qt 子控件伪类（如 `QMenuBar::item:selected`），二者语法不同，不可混用。