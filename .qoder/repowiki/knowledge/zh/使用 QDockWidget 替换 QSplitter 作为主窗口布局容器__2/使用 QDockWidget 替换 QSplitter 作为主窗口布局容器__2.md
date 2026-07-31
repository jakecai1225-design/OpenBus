---
kind: design
name: 使用 QDockWidget 替换 QSplitter 作为主窗口布局容器
source: session
category: adr
---

# 使用 QDockWidget 替换 QSplitter 作为主窗口布局容器

_来源：bf048c1 → 9c6ba9f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 MainWindow 使用 QSplitter 管理面板布局，导致面板无法独立关闭、拖拽和停靠，用户界面灵活性不足。需要支持更灵活的窗口布局能力。

## 决策驱动
- 可停靠性
- 用户自定义布局
- Qt 原生功能复用

## 备选方案
- **QSplitter 保持现状** _（已否决）_ — 优点：改动最小；缺点：不支持关闭/拖拽/停靠，用户体验受限
- **QDockWidget 替代方案** — 优点：原生支持关闭、拖拽、停靠，符合 Qt 标准应用模式；缺点：需要重写 createLayout() 方法，ActivityBar 需特殊处理固定宽度

## 决策
在 src/ui/mainwindow.cpp 中用 QDockWidget 完全替换 QSplitter，左侧 Dock 包含 ActivityBar 和 SideBar 的 QHBoxLayout 容器，右侧 Dock 为 RightPanel，底部 Dock 为 BottomPanel，中央为 QTabWidget(Trace/Graphic)。所有 Dock 设置 QDockWidget::AllDockWidgetFeatures。

## 影响
MainWindow 的 createLayout() 方法完全重写；ActivityBar 通过固定 48px 宽度和不可拖拽属性实现；菜单栏从 createToolBar() 迁移到 createMenuBar()；所有 QAction 成员被保留供菜单和侧边栏按钮共用。