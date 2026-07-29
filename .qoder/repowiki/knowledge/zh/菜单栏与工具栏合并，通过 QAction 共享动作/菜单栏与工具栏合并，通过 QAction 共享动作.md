---
kind: design
name: 菜单栏与工具栏合并，通过 QAction 共享动作
source: session
category: adr
---

# 菜单栏与工具栏合并，通过 QAction 共享动作

_来源：ec3fe3f → 3e55571 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 createToolBar() 中的录制/播放/清空等操作按钮与菜单功能重复，存在两套入口导致状态不同步。

## 决策驱动
- 单一事实来源
- 减少重复代码
- 菜单与按钮操作一致性

## 备选方案
- **保留独立工具栏** _（已否决）_ — 优点：操作快捷；缺点：与菜单重复，状态同步复杂
- **删除工具栏，复用 QAction** — 优点：单一入口，状态自动同步；缺点：失去快速按钮入口

## 决策
删除 createToolBar()，保留所有 QAction 成员供菜单栏和侧边栏按钮共用，通过信号槽机制统一触发。

## 影响
所有操作入口共享同一 QAction，状态一致；新增 CollapsibleSection 折叠区承载 DevicePanel 和 TraceConfigPanel 的控制控件。