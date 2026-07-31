---
kind: design
name: 采用 CollapsibleSection 组件组织侧边栏控件
source: session
category: adr
---

# 采用 CollapsibleSection 组件组织侧边栏控件

_来源：bf048c1 → 9c6ba9f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
DevicePanel 和 TraceConfigPanel 中的控制按钮（录制控制、回放控制）原本分散在工具栏中，需要重新组织到侧边栏的可折叠区域中，提升界面整洁度。

## 决策驱动
- 界面组织性
- 控件复用
- 用户操作集中化

## 备选方案
- **直接嵌入 QGroupBox** _（已否决）_ — 优点：简单直接；缺点：不支持折叠/展开交互，空间利用率低
- **自定义 CollapsibleSection 组件** — 优点：可复用、支持折叠/展开、标题栏可点击、内容容器灵活；缺点：需要新增组件类实现

## 决策
在 src/ui/panels/sidebarpanels.h/cpp 中创建 CollapsibleSection 类，提供可点击折叠/展开的标题栏和内容容器 widget。DevicePanel 添加'录制控制'折叠区，TraceConfigPanel 添加'回放控制'折叠区，将 m_seekSlider 和 m_speedCombo 从工具栏迁移到这些折叠区。

## 影响
DevicePanel 新增 recordToggled/clearRequested/autoScrollToggled 信号；TraceConfigPanel 新增 playRequested/pauseRequested/stopRequested/speedChanged/seekChanged 信号；MainWindow 通过槽函数连接这些信号处理播放控制逻辑。