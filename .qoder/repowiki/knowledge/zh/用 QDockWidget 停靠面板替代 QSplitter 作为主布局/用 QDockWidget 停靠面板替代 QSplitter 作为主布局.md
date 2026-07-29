---
kind: design
name: 用 QDockWidget 停靠面板替代 QSplitter 作为主布局
source: session
category: adr
---

# 用 QDockWidget 停靠面板替代 QSplitter 作为主布局

_来源：ec3fe3f → 3e55571 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 MainWindow 使用 QSplitter 进行固定分割布局，用户无法自定义界面；需要支持可关闭、可拖拽、可停靠的灵活布局。

## 决策驱动
- 用户自定义布局能力
- Qt 原生停靠面板支持
- 与主流桌面应用（如 Wireshark）一致的交互模式

## 备选方案
- **QSplitter 固定分割** _（已否决）_ — 优点：实现简单，代码改动小；缺点：用户无法隐藏/移动面板，灵活性差
- **QDockWidget 停靠面板** — 优点：支持关闭/拖拽/停靠/浮动，符合 Qt 桌面应用惯例；缺点：需要重构 createLayout 和面板容器结构

## 决策
用 QDockWidget 替换 QSplitter：左侧 Dock 包含 ActivityBar + SideBar，右侧 Dock 为 RightPanel，底部 Dock 为 BottomPanel，中央为 QTabWidget(Trace/Graphic)，所有 Dock 启用 AllDockWidgetFeatures。

## 影响
用户可自由调整面板布局；ActivityBar 保持固定 48px 宽度不可拖拽但整体 Dock 可关闭；MainWindow 需维护多个 Dock 的生命周期和信号连接。