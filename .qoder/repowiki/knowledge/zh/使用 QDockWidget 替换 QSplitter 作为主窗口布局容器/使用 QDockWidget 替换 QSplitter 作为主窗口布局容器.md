---
kind: design
name: 使用 QDockWidget 替换 QSplitter 作为主窗口布局容器
source: session
category: adr
---

# 使用 QDockWidget 替换 QSplitter 作为主窗口布局容器

_来源：6a9ceb9 → ba3315d 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 MainWindow 使用 QSplitter 管理面板布局，导致用户无法灵活调整界面布局（如关闭/停靠面板），且工具栏与侧边栏控件分散，交互不够直观。

## 决策驱动
- 用户可自定义布局（关闭/拖拽/停靠）
- 菜单栏与侧边栏按钮共享 QAction
- 符合专业分析工具（如 Wireshark）的界面范式

## 备选方案
- **QSplitter + QToolBar** _（已否决）_ — 优点：实现简单，代码改动小；缺点：用户无法隐藏/移动面板；工具栏占用顶部空间；扩展性差
- **QDockWidget 停靠面板** — 优点：支持所有 Dock 特性（可关闭、可拖拽、可停靠）；ActivityBar + SideBar 组合更紧凑；符合行业标准 UI 模式；缺点：需要重写 createLayout；CollapsibleSection 等新组件需开发

## 决策
在 MainWindow::createLayout() 中用 QDockWidget 完全替换 QSplitter：左侧 Dock 包含 ActivityBar(48px) 和 SideBar，右侧 Dock 为 RightPanel，底部 Dock 为 BottomPanel，中央为 QTabWidget(Trace/Graphic)。所有 Dock 启用 AllDockWidgetFeatures。

## 影响
用户可自由隐藏/恢复各面板；菜单栏与侧边栏共用 QAction 减少重复代码；新增 CollapsibleSection 组件用于侧边栏折叠区；ProjectPanel 重设计为项目上下文管理器；Trace 页面采用三栏布局（列表+帧信息+HexDump）。