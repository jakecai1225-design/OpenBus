---
kind: design
name: 重构 TraceView 为 Wireshark 风格的三栏布局
source: session
category: adr
---

# 重构 TraceView 为 Wireshark 风格的三栏布局

_来源：bf048c1 → 9c6ba9f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
现有 TraceView 功能单一，需要支持帧列表、解码信息和十六进制转储的同时查看，类似 Wireshark 的界面布局。

## 决策驱动
- 专业工具界面范式
- 信息密度
- 调试效率

## 备选方案
- **单页滚动视图** _（已否决）_ — 优点：实现简单；缺点：无法同时查看多个维度的帧信息
- **三栏分栏布局** — 优点：符合网络分析工具的标准界面模式，支持同时查看列表、解码和原始数据；缺点：需要拆分现有 traceview.cpp，新增 FrameInfoWidget 和 HexDumpWidget

## 决策
在 src/ui/traceview.h/cpp 中扩展 TraceTab widget，采用 FilterBar + TraceView(60%) + FrameInfo/HexDump(40%) 的三栏布局。FrameInfoWidget 使用 QTableWidget 显示解码字段，HexDumpWidget 使用 QPlainTextEdit 显示十六进制转储。选中 TraceView 行时自动更新底部两个面板。

## 影响
traceview.cpp 需要大幅重写以支持新布局；新增 FrameInfoWidget 和 HexDumpWidget 两个子组件；需要实现 DBC 解码集成以显示信号级别的解析结果；QSS 样式需要更新以适配新组件的视觉效果。