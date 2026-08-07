---
kind: design
name: SignalSendTab 重构为上下联动布局并支持枚举信号编辑
source: session
category: adr
---

# SignalSendTab 重构为上下联动布局并支持枚举信号编辑

_来源：77e1f61 → a9f172f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的 SignalSendTab 将 DBC 导入按钮放在顶部工具栏，且信号编辑器仅支持数值输入，无法直观处理 DBC 中定义的枚举信号。需要重新组织界面以改善 CAN 帧发送的交互体验。

## 决策驱动
- 提升 DBC 消息导入效率
- 让枚举信号以可读下拉框呈现
- 表格与编辑器双向同步减少重复输入

## 备选方案
- **保持原有单页布局，仅增加枚举下拉** — 优点：改动最小；缺点：DBC 导入入口位置不直观；表格与编辑器无联动，用户需手动复制 ID/周期等字段
- **拆分为独立对话框** — 优点：职责清晰；缺点：增加窗口管理复杂度；打断当前工作流
- **上下分栏 + 行选择联动 + 枚举 QComboBox** — 优点：选中行自动填充底部编辑器；枚举值通过 valueTable 生成带描述的选项；从 DBC 批量导入时直接生成发送列表行；缺点：需维护 m_signalComboBoxes / m_signalWidgets 等控件集合的生命周期

## 决策
将 SignalSendTab 重构为上下分栏：上半部分保留 Send Table（列表发送/停止/清空），下半部分集中 Raw 编辑区与按信号列出的编辑器；新增 DbcImportDialog 作为模态选择器，通过 DbcManager 临时加载 .dbc 文件并以树形结构展示 Message→Signal，返回选中的 CAN ID 列表后在发送表中逐条添加行。信号编辑器根据信号的 valueTable 动态创建 QComboBox（显示 "value - description"）或回退到 QDoubleSpinBox，并在 updateDataFromSignals/updateSignalsFromData 中分别处理 rawValue 与物理值的编解码。

## 影响
DbcImportDialog 新增为独立 UI 模块并加入 CMakeLists.txt 的 SRC_UI；signalsendtab 新增 onSendTableRowChanged、m_selectedRow、m_signalComboBoxes、m_signalWidgets 等成员以支撑联动；枚举信号的用户体验显著提升但需额外维护控件映射；若 DBC 中 valueTable 为空则仍走数值路径，向后兼容旧行为。