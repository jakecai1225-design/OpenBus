---
kind: design
name: 引入 ProjectContext 数据结构管理工程上下文
source: session
category: adr
---

# 引入 ProjectContext 数据结构管理工程上下文

_来源：bf048c1 → 9c6ba9f 提交周期内记录的编码计划——内容为规划时意图，实现可能滞后或有出入。_

**状态：** accepted

## 背景
ProjectPanel 需要从文件树结构改为项目上下文管理器，支持多工程切换、DBC 文件和录制文件的关联管理。

## 决策驱动
- 工程状态管理
- 数据持久化
- 多工程支持

## 备选方案
- **扁平化的 QString 列表存储** _（已否决）_ — 优点：结构简单；缺点：无法表达工程与资源的关联关系
- **结构化 ProjectContext 对象** — 优点：清晰表达工程-资源关系，便于序列化/反序列化，支持扩展字段；缺点：需要定义新的数据结构

## 决策
在 sidebarpanels.h 中定义 ProjectContext 结构体，包含 name、dbcFiles、recordFiles、layoutConfig 四个字段。ProjectPanel 维护 QList<ProjectContext>，通过 projectSwitched/projectCreated 信号通知 MainWindow 工程切换事件。

## 影响
ProjectPanel 的 UI 从文件树(QTreeWidget)改为项目列表(QListWidget)，新增新建/保存/删除工程的按钮；工程配置需要通过 layoutConfig 字段进行序列化存储；后续需要实现工程数据的持久化机制。