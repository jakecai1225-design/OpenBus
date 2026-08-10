---
kind: design
name: SignalSendTab 采用上下分栏布局并引入 DBC 导入对话框
source: session
category: adr
---

# SignalSendTab 采用上下分栏布局并引入 DBC 导入对话框

_来源：c6770cc → 902386f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的 SignalSendTab 将发送列表与信号编辑混在同一区域，操作入口分散（如从 DBC 导入按钮在顶部工具栏），且不支持枚举信号的可视化编辑。需要重新组织界面以支持批量发送、实时同步和更直观的信号输入。

## 决策驱动
- 提升可发现性：将常用操作集中在顶部工具栏（列表发送/停止/清空）
- 降低误操作：通过行选择联动底部编辑器，避免重复添加相同 CAN ID
- 增强信号输入体验：枚举信号用 QComboBox 展示 value-description 对

## 备选方案
- **保持原有扁平布局，仅增加功能** _（已否决）_ — 优点：改动最小；缺点：操作入口仍分散；枚举信号只能以数值输入，易出错
- **拆分为独立页面 + 弹窗式 DBC 导入** _（已否决）_ — 优点：职责清晰；缺点：跨页面切换成本高；当前 Tab 内已具备完整发送流程，拆分收益有限
- **上下分栏：上半部发送表格 + 下半部 Raw+信号编辑器，新增模态 DbcImportDialog** — 优点：同一视图完成全部操作；行选择与编辑器双向同步；枚举信号可视化；DBC 导入集中管理；缺点：重构工作量较大；需维护选中行状态 m_selectedRow 与控件集合映射

## 决策
将 SignalSendTab 重构为上下分栏布局：上半部分保留 Send Table（支持列表发送/停止/清空），下半部分提供 Raw 编辑行与按信号维度的编辑器（QDoubleSpinBox 或 QComboBox），并通过 onSendTableRowChanged() 实现行选择与编辑器的双向同步；新增 DbcImportDialog 作为模态对话框，基于 DbcManager 临时加载 DBC 文件并以树形结构展示 Message/Signal，用户多选后批量添加到发送列表。

## 影响
UI 层新增 src/ui/dbcimportdialog.h/.cpp 并在 CMakeLists.txt 中注册；signalsendtab.cpp 需维护 m_selectedRow、m_signalComboBoxes、m_signalWidgets 等状态；枚举信号编码/解码逻辑从统一 encode/decode 分支到 ComboBox/SpinBox 两条路径；后续若扩展更多信号类型，需在 rebuildSignalEditors 中继续分支处理。