---
kind: design
name: SignalSendTab 重构为上下联动编辑模式
source: session
category: adr
---

# SignalSendTab 重构为上下联动编辑模式

_来源：bf048c1 → 9c6ba9f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
原有的 SignalSendTab 界面不够直观，发送列表和信号编辑器之间缺乏联动，枚举信号的支持也不完善。

## 决策驱动
- 用户交互流畅性
- 枚举信号支持
- DBC 导入便利性

## 备选方案
- **保持原有单页面设计** _（已否决）_ — 优点：改动最小；缺点：无法支持枚举信号，缺少上下联动
- **上下分栏 + 实时联动** — 优点：顶部发送列表，底部 Raw 编辑和信号编辑，点击行自动同步编辑器，支持枚举下拉框；缺点：需要重写大部分 signalsendtab.cpp 逻辑，新增 dbcimportdialog

## 决策
重构 src/ui/signalsendtab.h/cpp：顶部工具栏包含列表发送/停止/清空按钮；上半部分为 Send Table 显示发送帧列表；下半部分分为 Raw 编辑行和信号编辑列表。新增 src/ui/dbcimportdialog.h/cpp 实现 DBC 导入选择对话框。当点击发送列表某行时，onSendTableRowChanged() 自动填充底部编辑器并重建信号编辑器。

## 影响
signalsendtab.cpp 的 rebuildSignalEditors() 需要支持 QComboBox 用于枚举信号；updateDataFromSignals() 和 updateSignalsFromData() 需要分别处理 SpinBox 和 ComboBox 的数据转换；DbcImportDialog 依赖 core/dbcmanager.h 和 core/dbcdata.h；CMakeLists.txt 需要添加新文件到 SRC_UI 列表。