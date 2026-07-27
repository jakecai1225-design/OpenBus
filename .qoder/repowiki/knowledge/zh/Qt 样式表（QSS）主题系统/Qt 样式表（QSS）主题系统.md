---
kind: frontend_style
name: Qt 样式表（QSS）主题系统
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - src/main.cpp
    - resources/resources.qrc
    - src/ui/mainwindow.ui
---

本项目采用 Qt 样式表（QSS）作为前端 UI 样式方案，通过资源文件集中管理样式，并在应用启动时全局加载。

**系统与工具**
- 使用 Qt6 Widgets 框架的 QSS（Qt Style Sheets）机制进行界面样式定义，语法与 CSS 高度相似。
- 样式文件以 `.qss` 后缀存放于 `resources/styles/default.qss`，并通过 Qt 资源系统（`.qrc`）打包进可执行文件。
- 在 `src/main.cpp` 中通过 `QFile` 读取 `:/styles/default.qss` 并调用 `app.setStyleSheet()` 全局生效。

**核心文件**
- `resources/styles/default.qss`：唯一样式源，定义了 QMainWindow、QLabel、QPushButton 及其 hover/pressed/disabled 状态的颜色、边框、圆角、内边距和字体大小。
- `src/main.cpp`：应用入口，负责加载 QSS 样式表并设置到 QApplication。
- `resources/resources.qrc`：Qt 资源清单，将样式文件编译进二进制。
- `src/ui/mainwindow.ui`：由 Qt Designer 生成的界面布局文件，配合 `ui->setupUi(this)` 构建主窗口。

**架构与约定**
- 样式与代码分离：所有视觉样式集中在 `default.qss`，C++ 逻辑不掺杂任何硬编码颜色或尺寸。
- 全局样式策略：通过 `QApplication::setStyleSheet` 一次性加载，作用于整个应用树。
- 组件级样式覆盖：QSS 选择器针对具体 Widget 类型（如 QPushButton）定义默认外观，未覆盖的控件保持 Qt 原生样式。
- 交互状态分层：通过伪类 `:hover`、`:pressed`、`:disabled` 分别定义按钮的悬停、按下、禁用态视觉反馈。

**约束与规范**
- 样式文件必须放在 `resources/styles/` 目录下并通过 `:/styles/` 路径引用。
- 颜色值统一使用十六进制格式（如 `#4a90d9`、`#f5f5f5`），未引入设计令牌或变量机制。
- 当前仅对基础控件（QMainWindow、QLabel、QPushButton）定义了样式，其他控件沿用 Qt 默认外观。