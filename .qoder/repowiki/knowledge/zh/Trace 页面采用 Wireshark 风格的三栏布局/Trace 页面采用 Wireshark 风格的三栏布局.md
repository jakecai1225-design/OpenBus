---
kind: design
name: Trace 页面采用 Wireshark 风格的三栏布局
source: session
category: adr
---

# Trace 页面采用 Wireshark 风格的三栏布局

_来源：5786c88 → 1ab4a32 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 TraceView 仅显示列表，缺少帧详情和原始数据查看能力。需要类似 Wireshark 的三栏布局提升分析效率。

## 决策驱动
- 信息密度高
- 同时查看列表/详情/原始数据
- 行业标准布局

## 备选方案
- **单列列表 + 弹窗详情** _（已否决）_ — 优点：实现简单；缺点：频繁切换上下文，分析效率低
- **Wireshark 三栏布局** — 优点：顶部 FilterBar 过滤；60% 区域 TraceView 列表；底部 40% 分两栏分别显示 FrameInfo(QTableWidget) 和 HexDump(QPlainTextEdit)；选中行时同步更新详情；缺点：布局复杂度较高；需要实现两个子 widget

## 决策
在 traceview.h/cpp 中扩展 TraceTab widget：FilterBar + TraceView(60%) + 底部双栏(FrameInfoWidget 显示解码字段, HexDumpWidget 显示 Hex+ASCII)。选中 TraceView 行时自动更新底部两个面板。

## 影响
FrameInfoWidget 使用 QTableWidget 显示 Time/Channel/Dir/ID/DLC/Data/Flags 字段，如有 DBC 则显示信号解码值；HexDumpWidget 使用 QPlainTextEdit 显示带偏移的 Hex dump；需要在 default.qss 中添加等宽字体样式。