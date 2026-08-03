---
kind: design
name: SignalSendTab 采用上下分栏布局与 DBC 导入对话框
source: session
category: adr
---

# SignalSendTab 采用上下分栏布局与 DBC 导入对话框

_来源：b6a3db7 → b462556 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的 SignalSendTab 发送页面功能集中在单一区域，缺乏清晰的层次结构；用户需要同时编辑帧的原始数据和按信号维度操作，且频繁从 DBC 文件导入 CAN 消息时操作路径过长。

## 决策驱动
- UI 层次清晰（列表在上、编辑在下）
- 支持枚举信号的直观选择
- DBC 导入流程独立为模态对话框
- 表格行选择与编辑器双向联动

## 备选方案
- **上下分栏 + 独立 DbcImportDialog** — 优点：发送列表与编辑区职责分离；枚举信号用 QComboBox 更直观；DBC 搜索/过滤在独立窗口中完成；通过 m_selectedRow 实现行选择同步；缺点：需要维护两个区域的数据一致性；新增 ui/dbcimportdialog.h/.cpp 模块
- **保持原有单页布局，仅增强功能** _（已否决）_ — 优点：改动最小；缺点：界面拥挤；枚举信号只能以数值输入；DBC 导入入口不显眼

## 决策
重构 SignalSendTab 为上下分栏：上半部分为 Send Table（发送帧列表），下半部分包含 Raw 编辑行和信号编辑列表；将 DBC 导入功能抽取为独立的 DbcImportDialog 模态对话框；通过 onSendTableRowChanged() 实现表格行选择与底部编辑器的双向同步；rebuildSignalEditors() 根据信号 valueTable 动态生成 QComboBox（枚举）或 QDoubleSpinBox（数值）。

## 影响
新增 src/ui/dbcimportdialog.h/.cpp 两个文件并在 CMakeLists.txt 注册；signalsendtab.cpp 中增加 m_selectedRow、m_signalComboBoxes、m_signalWidgets 等成员变量；表格行选择变化会触发编辑器重建，增加了 UI 状态同步的复杂度但提升了可操作性和可读性。