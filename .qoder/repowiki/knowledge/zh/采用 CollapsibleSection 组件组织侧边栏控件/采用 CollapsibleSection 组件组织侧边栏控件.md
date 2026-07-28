---
kind: design
name: 采用 CollapsibleSection 组件组织侧边栏控件
source: session
category: adr
---

# 采用 CollapsibleSection 组件组织侧边栏控件

_来源：5786c88 → 1ab4a32 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
DevicePanel 和 TraceConfigPanel 中的录制/回放控件原本位于工具栏，需要迁移到侧边栏并以折叠区形式组织，避免界面拥挤。

## 决策驱动
- 控件分组清晰
- 节省垂直空间
- 可复用折叠逻辑

## 备选方案
- **直接放入 QGroupBox** _（已否决）_ — 优点：Qt 原生支持，无需额外代码；缺点：无折叠动画，标题栏不可点击交互
- **自定义 CollapsibleSection 组件** — 优点：标题栏可点击折叠/展开；内容容器可复用；样式统一；可在多个 Panel 中复用；缺点：需要新增组件类；增加少量维护成本

## 决策
新建 CollapsibleSection 组件（sidebarpanels.h/cpp），提供可点击折叠/展开的标题栏和内容容器。DevicePanel 添加'录制控制'折叠区，TraceConfigPanel 添加'回放控制'折叠区。

## 影响
m_seekSlider 和 m_speedCombo 从工具栏迁移到 TraceConfigPanel 的折叠区；新增 playRequested/pauseRequested/stopRequested/speedChanged/seekChanged 等信号通知 MainWindow；DevicePanel 新增 recordToggled/clearRequested/autoScrollToggled 信号。