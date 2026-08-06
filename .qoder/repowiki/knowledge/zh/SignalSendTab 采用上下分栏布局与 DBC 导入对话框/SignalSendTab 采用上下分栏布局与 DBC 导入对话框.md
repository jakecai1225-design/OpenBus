---
kind: design
name: SignalSendTab 采用上下分栏布局与 DBC 导入对话框
source: session
category: adr
---

# SignalSendTab 采用上下分栏布局与 DBC 导入对话框

_来源：144419a → 2292a6a 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的发送页面功能集中在单一区域，用户需要频繁切换编辑模式（Raw 数据 vs 信号值），且从 DBC 文件批量导入帧的操作入口不够直观。需要重新组织 UI 结构以提升 CAN 帧配置效率。

## 决策驱动
- 提升 CAN 帧配置效率
- 降低 Raw 数据与信号值之间的切换成本
- 简化 DBC 文件导入流程

## 备选方案
- **上下分栏 + 独立 DBC 导入弹框** — 优点：发送列表与编辑区职责分离；枚举信号通过 QComboBox 提供友好选择；DBC 导入支持按 ID/名称搜索过滤；缺点：新增 DbcImportDialog 类增加代码量；需要维护表格行与编辑器之间的双向同步逻辑
- **保持原有单页布局仅增强功能** _（已否决）_ — 优点：改动最小；缺点：无法解决 Raw 与信号编辑切换的交互问题；DBC 导入入口不明显

## 决策
将 SignalSendTab 重构为上下分栏：上半部分为发送帧列表（支持全选/停止/清空），下半部分包含 Raw 编辑行和信号编辑器（根据 valueTable 自动选择 QDoubleSpinBox 或 QComboBox）；同时新建独立的 DbcImportDialog 模态对话框，通过 QTreeWidget 展示 DBC 文件中的 Message 和 Signal，支持实时搜索过滤。

## 影响
signalsendtab.cpp 中增加了 onSendTableRowChanged() 实现表格行与编辑器的双向联动；rebuildSignalEditors() 需区分数值型与枚举型信号创建不同控件；DbcImportDialog 依赖 core/dbcmanager.h 临时加载 DBC 文件；CMakeLists.txt 需注册新源文件。枚举信号的数据编解码需在 updateDataFromSignals()/updateSignalsFromData() 中分别处理 rawValue 与物理值的转换。