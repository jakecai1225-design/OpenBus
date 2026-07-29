---
kind: design
name: Trace 页面采用 Wireshark 风格三栏布局
source: session
category: adr
---

# Trace 页面采用 Wireshark 风格三栏布局

_来源：ec3fe3f → 3e55571 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 TraceView 仅显示列表，帧详情和原始数据分散在不同位置，不利于协议分析。

## 决策驱动
- 协议分析效率
- 与行业工具习惯一致
- 信息密度最大化

## 备选方案
- **单列表 + 弹窗详情** _（已否决）_ — 优点：实现简单；缺点：频繁切换上下文，效率低
- **Wireshark 三栏布局** — 优点：列表+解码信息+Hex 同时可见，选中即更新；缺点：需要拆分 traceview.h/cpp 或新增文件

## 决策
在 TraceTab 中实现 FilterBar + TraceView(60%) + FrameInfoWidget/QSplitter/HexDumpWidget(40%) 的三栏布局，选中行时联动更新底部两个面板。

## 影响
FrameInfoWidget 使用 QTableWidget 显示 Time/Channel/Dir/ID/DLC/Data/Flags 及 DBC 信号解码值；HexDumpWidget 使用 QPlainTextEdit 显示偏移+Hex+ASCII。