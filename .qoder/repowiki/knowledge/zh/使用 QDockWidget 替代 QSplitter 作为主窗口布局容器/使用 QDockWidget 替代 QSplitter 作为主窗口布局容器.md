---
kind: design
name: 使用 QDockWidget 替代 QSplitter 作为主窗口布局容器
source: session
category: adr
---

# 使用 QDockWidget 替代 QSplitter 作为主窗口布局容器

_来源：5786c88 → 1ab4a32 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 MainWindow 使用 QSplitter 管理面板布局，功能受限且不支持停靠/浮动。需要支持用户自定义布局、面板可关闭/拖拽/停靠的现代化 UI 体验。

## 决策驱动
- 用户可定制布局
- 面板可独立浮动/停靠
- 与 Qt 标准 IDE 风格一致

## 备选方案
- **QSplitter 继续扩展** _（已否决）_ — 优点：改动最小，无需重构现有代码；缺点：不支持停靠/浮动，无法实现可关闭的面板，用户体验受限
- **QDockWidget 停靠面板** — 优点：原生支持可关闭、可拖拽、可停靠；符合 Qt 标准应用模式；ActivityBar 固定宽度但整体 Dock 可控制；缺点：需要重写 createLayout()；需要调整信号连接以适配新布局

## 决策
用 QDockWidget 替换 QSplitter：左侧 Dock 包含 ActivityBar + SideBar，右侧 Dock 为 RightPanel，底部 Dock 为 BottomPanel，中央为 QTabWidget(Trace/Graphic)。所有 Dock 启用 AllDockWidgetFeatures。

## 影响
菜单栏 createToolBar() 被删除，AAction 成员保留供菜单和侧边栏共用；ActivityBar 固定 48px 不可拖拽但整个左侧 Dock 可关闭/停靠；需要更新 onActivityChanged() 以联动标签页切换。