---
kind: design
name: SignalSendTab 重构为上下分栏布局并引入 DbcImportDialog
source: session
category: adr
---

# SignalSendTab 重构为上下分栏布局并引入 DbcImportDialog

_来源：cae7f78 → 360ba61 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有 SignalSendTab 将 DBC 导入按钮放在顶部工具栏，且信号编辑器仅支持数值输入，无法表达枚举信号；发送列表与编辑区之间缺乏联动，用户难以快速从 DBC 定义批量创建发送帧。

## 决策驱动
- 提升 CAN 调试效率（从 DBC 直接导入 Message）
- 让枚举信号以下拉菜单呈现，避免手动换算 raw/phys
- 表格行选择与底部编辑器双向同步，减少重复输入

## 备选方案
- **保持原布局不变，仅增加枚举支持** _（已否决）_ — 优点：改动最小；缺点：无法解决 DBC 导入入口隐蔽、列表与编辑器割裂的问题
- **拆分为独立页面：DBC 浏览页 + 发送配置页** _（已否决）_ — 优点：职责更清晰；缺点：增加导航复杂度，打断现有工作流
- **在现有 Tab 内重构为上下分栏 + 新增 DbcImportDialog** — 优点：保留单页工作流，通过 QTreeWidget 搜索已加载 DBC 的 Message/Signal，按 CAN ID 或信号名过滤；选中行变化时自动填充底部编辑器；valueTable 非空时渲染 QComboBox；缺点：需要维护 m_signalComboBoxes / m_signalWidgets 等额外状态

## 决策
将 SignalSendTab 重构成「上半部分 Send Table + 下半部分 Raw 编辑行 + 信号编辑列表」的分栏布局，并将「从 DBC 导入」移入底部编辑区；新建 src/ui/dbcimportdialog.h/.cpp 作为模态 QDialog，通过 DbcManager 临时加载 .dbc 并以 QTreeWidget 展示 DBC→Message→Signal 树，支持按 CAN ID 或信号名实时搜索，确认后返回选中的 CAN ID 列表用于批量创建发送行。同时扩展 rebuildSignalEditors/updateDataFromSignals/updateSignalsFromData 以根据 valueTable 动态切换 QDoubleSpinBox 与 QComboBox。

## 影响
DbcImportDialog 新增为独立 UI 模块，需在 CMakeLists.txt 中注册；SignalSendTab 新增 m_selectedRow、m_signalComboBoxes、m_signalWidgets 等成员以维持表格-编辑器联动；枚举信号的编码/解码路径分支到 QComboBox 逻辑，需确保 valueTable 与 DBC 描述一致；批量导入时默认数据全零、周期取 cycleTime、次数 ∞，后续仍需人工调整。