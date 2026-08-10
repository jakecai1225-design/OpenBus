---
kind: design
name: SignalSendTab 重构为上下分栏布局并支持 DBC 导入与枚举信号编辑
source: session
category: adr
---

# SignalSendTab 重构为上下分栏布局并支持 DBC 导入与枚举信号编辑

_来源：287163e → cae7f78 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的 SignalSendTab 将发送列表、原始数据编辑和信号编辑混在一起，操作路径不清晰；同时缺少从 DBC 文件批量导入 Message 的能力，且数值型信号无法以枚举下拉形式编辑。需要重新组织 UI 并增强交互。

## 决策驱动
- 提升可发现性：把发送列表放在上半区、编辑区集中在下半区
- 降低重复录入成本：通过 DBC 文件一键导入 Message
- 提高输入正确性：对带 valueTable 的信号使用 QComboBox 展示枚举值

## 备选方案
- **保持原有扁平布局，仅增加功能** _（已否决）_ — 优点：改动最小；缺点：工具栏拥挤、操作路径长、新增的 DBC 导入入口无处安放
- **拆分为多个独立 Tab（发送列表、信号编辑器、DBC 导入）** _（已否决）_ — 优点：职责更清晰；缺点：上下文切换成本高，用户期望在一个页面完成“选帧-改信号-发送”的闭环
- **上下分栏 + 行选择联动 + DBC 导入弹窗** — 优点：一次打开即可看到列表和编辑器，点击行自动同步底部编辑器；通过模态 DbcImportDialog 复用已有 DbcManager；缺点：signalsendtab.cpp 逻辑量增大，需维护选中行状态 m_selectedRow

## 决策
将 SignalSendTab 重构为上下分栏：顶部工具栏保留列表级操作（列表发送/停止/清空），上半区为 Send Table，下半区集中 Raw 数据编辑与按信号维度的编辑器；新增 src/ui/dbcimportdialog.h/.cpp 作为模态对话框，通过 DbcManager 临时加载 .dbc 文件并以 TreeWidget 展示 Message→Signal 层级，支持按 CAN ID 或信号名搜索，确认后返回 selectedCanIds() 批量添加到发送列表；在 rebuildSignalEditors() 中根据信号的 valueTable 动态创建 QComboBox 而非 QDoubleSpinBox，并在 updateDataFromSignals/updateSignalsFromData 中处理 rawValue↔phys 的双向转换；通过 onSendTableRowChanged 实现表格行选择与底部编辑器的双向联动。

## 影响
UI 结构更直观，但 signalsendtab.cpp 复杂度上升，需维护 m_selectedRow、m_signalComboBoxes、m_signalWidgets 等状态；DbcImportDialog 引入新的依赖 core/dbcmanager.h、core/dbcdata.h，需在 CMakeLists.txt 中注册新源文件；枚举信号编辑提升了易用性，但也要求 DBC 中的 valueTable 描述准确，否则 ComboBox 选项无意义。