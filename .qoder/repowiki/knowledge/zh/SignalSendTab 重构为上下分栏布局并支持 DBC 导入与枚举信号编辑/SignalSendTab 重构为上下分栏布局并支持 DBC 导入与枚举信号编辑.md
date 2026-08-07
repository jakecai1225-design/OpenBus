---
kind: design
name: SignalSendTab 重构为上下分栏布局并支持 DBC 导入与枚举信号编辑
source: session
category: adr
---

# SignalSendTab 重构为上下分栏布局并支持 DBC 导入与枚举信号编辑

_来源：074e972 → 77e1f61 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的 SignalSendTab 将发送列表、原始数据编辑和信号编辑混在一起，操作路径不清晰；同时缺乏从 DBC 文件批量导入 Message 的能力，也不支持枚举类型信号的可视化编辑。需要重新组织界面并补齐这些能力。

## 决策驱动
- 提升可发现性：将发送列表与编辑区分离到上下两部分
- 减少重复操作：表格行选择变化时自动同步底部编辑器
- 降低手工输入成本：通过 DbcImportDialog 按 CAN ID/信号名搜索并批量导入
- 正确表达信号语义：枚举信号用 QComboBox 展示 value-description 而非裸数值

## 备选方案
- **保持原有单页布局，仅增加功能** _（已否决）_ — 优点：改动最小；缺点：工具栏臃肿，用户难以区分“全部发送”和“单帧发送”，无法自然支持枚举信号的下拉选择
- **新建独立页面承载 DBC 导入** _（已否决）_ — 优点：职责清晰；缺点：与现有 Send Table 割裂，用户需要在多个窗口间切换
- **在 SignalSendTab 内新增 DbcImportDialog + 枚举信号编辑器** — 优点：在同一工作流中完成导入→编辑→发送，复用现有 DbcManager 与 DbcData；缺点：signalsendtab.cpp 体积增大，需维护 m_signalComboBoxes / m_signalWidgets 等状态映射

## 决策
对 `src/ui/signalsendtab.h/.cpp` 进行结构重组：顶部工具栏改为「列表发送/列表停止/清空列表」，上半部分保留 Send Table，下半部分拆出 Raw 编辑行与信号编辑列表；新增 `src/ui/dbcimportdialog.h/.cpp` 作为模态对话框，通过 `DbcManager` 临时加载 .dbc 文件并按 CAN ID/信号名过滤树节点，返回选中 CAN ID 后由 SignalSendTab 创建对应发送行；信号编辑器根据 `valueTable` 是否为空动态创建 `QComboBox`（枚举）或 `QDoubleSpinBox`（数值），并在 `updateDataFromSignals()` / `updateSignalsFromData()` 中分别处理 rawValue 与物理值的编解码。

## 影响
SignalSendTab 的 UI 逻辑集中在一个类中，维护 `m_selectedRow`、`m_signalComboBoxes`、`m_signalWidgets` 等状态，增加了耦合度但简化了跨控件同步；DBC 导入依赖 `core/dbcmanager.h` 与 `core/dbcdata.h`，若 DBC 数据结构变更需要同步更新；枚举信号的正确显示提升了调试效率，但需在后续扩展中确保所有新信号类型都能被 `rebuildSignalEditors()` 识别。