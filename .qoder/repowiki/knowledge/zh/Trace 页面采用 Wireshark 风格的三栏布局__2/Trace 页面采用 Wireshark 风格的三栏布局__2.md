---
kind: design
name: Trace 页面采用 Wireshark 风格的三栏布局
source: session
category: adr
---

# Trace 页面采用 Wireshark 风格的三栏布局

_来源：6a9ceb9 → ba3315d 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 TraceView 仅显示帧列表，缺少帧详情和原始数据查看能力，调试效率低。

## 决策驱动
- 同时查看帧列表、解码字段和原始 Hex 数据
- 选中行联动更新详情面板
- 支持 DBC 信号解码值显示

## 备选方案
- **单列表 + 弹窗详情** _（已否决）_ — 优点：实现简单；缺点：频繁切换窗口打断工作流；无法同时对比多个帧
- **三栏并排布局 (60%列表 + 20%帧信息 + 20%Hex)** — 优点：一次可见全部关键信息；选中行自动联动；符合 Wireshark 等成熟工具的交互习惯；缺点：窄屏下可读性下降；FrameInfoWidget 和 HexDumpWidget 需分别实现

## 决策
新建 TraceTab widget，顶部 FilterBar，中间 TraceView(60%)，底部左右分栏：FrameInfoWidget(QTableWidget 显示 Time/Channel/Dir/ID/DLC/Data/Flags 及 DBC 信号值) 和 HexDumpWidget(QPlainTextEdit 显示偏移+Hex+ASCII)。选中 TraceView 行时同步更新底部两个面板。

## 影响
调试效率显著提升，无需切换窗口即可对照原始数据和解析结果；FrameInfoWidget 依赖 DBC 文件提供信号级解码；HexDumpWidget 使用等宽字体便于对齐。