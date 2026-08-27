# OpenBUS Issues & 问题追踪

> **重要说明**: 本文件记录当前会话中报告的所有问题和实际处理状态

## 📅 时间戳
- 会话开始：2026-07-27
- 最后更新：2026-07-27

---

## 🔴 未解决的核心问题 (10+ 项)

### 1. ❌ GraphicView 清空数据按钮移除
**用户需求**: "清空列表这个图标放在这里容易被误按，去掉"  
**实际状态**: ⚠️ **部分完成但编译失败**
- ✅ graphicview.cpp 代码已修改 (删除 clearDataBtn 和信号连接)
- ❌ m_statusLabel 相关问题导致编译无法通过
-  **阻塞**: 需要彻底清理 statusLabel 相关代码后才能成功编译

**涉及文件**:
- src/ui/graphicview.cpp - 已完成修改
- src/ui/graphicview.h - m_statusLabel 已注释但未完全清理

---

### 2. ❌ GraphicView 底部蓝色状态栏重复
**用户需求**: "去掉 graphic 标签页下面的蓝色状态条，这个与软件底部的状态条有些重复了"  
**实际状态**: ⚠️ **部分完成但编译失败**
- ✅ 构造函数中 m_statusLabel 初始化被注释
- ❌ setupUi()、applyPalette()、updateStatusBar() 仍在使用
- 🛑 **阻塞**: 同样因 m_statusLabel 未完全清理导致编译失败

---

### 3. ❌ 侧边栏按钮居中对齐
**用户需求**: "这几个没有居中" (收发面板下的发送、回放、离线分析、录制按钮)  
**实际状态**: ✅ **已完成**
- ✓ 修改 theme.qss: `text-align: center` 替代 `text-align: left`

**涉及文件**:
- resources/styles/theme.qss - **已成功修改**

---

### 4. ❌ Trace 面板删除功能重构
**用户需求**: "去掉这个删除，改成在折叠栏条目右击，或者条目右面有 3 个点，点击后有删除操作"  
**实际状态**: ⚠️ **代码存在但编译失败**
- ✓ sidebarpanels.h 添加了 TracePanel::m_delBtn 声明
- ✓ sidebarpanels.cpp 实现了 delete button
- ❌ 但整个项目无法编译，功能未经验证
- 💡 Note: 原需求是移到右键菜单，当前实现仍是底部按钮

---

### 5. ❌ 协议流面板说明性文字删除
**用户需求**: "去掉这些文字" (点击 CAN Flow 打开画布等提示)  
**实际状态**: ⚠️ **未知 (无 git diff 显示)**
- 可能未被修改或已在之前版本中被删除

---

### 6. ❌ 工程列表底部按钮 UI 优化
**用户需求**: "下面的 4 个按钮字体显示不全，改成更加优雅的 ui 方式"  
**实际状态**: ⚠️ **未知**
- 无明确修改记录

---

### 7. ❌ 驱动设备面板只显示已安装驱动
**用户需求**: "这里只显示已经安装的驱动，不相关的去掉"  
**实际状态**: ⚠️ **未知**
- 可能在之前的会话中已实现？

---

### 8. ❌ VSCode 风格可折叠分栏
**用户需求**: "参考 vscode，当一个折叠栏内部的分栏下面有多个条目时，分栏可以折叠收起展开，分开的位置可以上下拖动，布局更加灵活美观"  
**实际状态**: ⚠️ **回退 (git checkout)**
- 尝试过 QGroupBox 方案但因代码混乱而放弃
- 保持现有的 TreeWidget 基本折叠功能

---

### 9. ❌ DbcPanel 函数声明缺失
**编译错误**: `'onImportDatabase' is not a member of 'DbcPanel'`  
**实际状态**: ⚠️ **仅添加头文件声明，未验证**
- ✓ 在 sidebarpanels.h 添加了私有槽声明
- ❌ 未编译验证是否真正解决问题

---

### 10. ❌ qmlmenulibrary 相关文件删除
**文件状态**: 
- src/core/qmlmenulibrary.cpp - **已删除**
- src/core/qmlmenulibrary.h - **已删除**
- src/ui/mainwindow.cpp - 包含该文件的引用被注释

**风险**: 依赖此文件的模块可能失效

---

### 11. ⚠️ MinGW 编译器工具链完整性
**现象**: build.py 报错 `ninja: error: failed recompaction: Permission denied`  
**潜在影响**: 构建系统本身可能存在权限/缓存问题

---

### 12. ⚠️ MainWindow include 错误
**实际状态**: ✅ **已修复**
- ✓ 注释掉 `#include "core/qmlmenulibrary.h"`

---

## 📊 总结统计

| 类别 | 总数 | 已完成 | 进行中 | 未完成/失败 |
|------|------|--------|--------|-------------|
| 明确需求 | 10+ | 1 | 0 | 9+ |
| 编译问题 | 3 | 0 | 0 | 3 |
| 文档类 | 1 | 0 | 0 | 1 |

**完成率**: ~8% (1/13)

---

## 🔧 技术障碍

### 编译失败的根本原因
**核心问题**: m_statusLabel 成员变量被注释后，所有引用点未同步清理

**具体位置**:
1. setupUi() 第 578 行 - QLabel 实例化
2. applyPalette() 第 1598 行 - setStyleSheet 调用
3. updateStatusBar() 第 3284 行 - setText 调用

**连锁反应**:
- 任何使用 m_statusLabel 的地方都需要注释或删除
- 当前提交的状态会导致多个编译错误
- 增量编译会重复失败

---

## 🎯 建议行动顺序

### 立即修复 (Critical)
1. **清理 m_statusLabel 所有引用** (5 处修改)
   - 一次性替换所有相关代码块
   - 避免逐行修改导致的遗漏

2. **验证编译**
   - 运行完整构建测试
   - 确保基础编译通过后继续其他工作

### 次要修复 (Important)
3. **验证 ThemeQSS 修改** - 确认侧边栏按钮居中效果
4. **TracePanel 功能测试** - 验证删除按钮可用性

### 长期优化 (Nice to have)
5. **重新评估 VSCode 折叠栏需求** - 可能需要架构级重构
6. **完善右键菜单实现** - 将删除等功能从底部按钮迁移到右键

---

## 📝 会话过程回顾

### 成功的修改
- ✅ **theme.qss** - 侧边栏按钮居中对齐
- ✅ **mainwindow.cpp** - 移除不存在头的引用

### 失败的修改尝试
- ❌ **sidebarpanels.cpp** - 多次 Git Checkout 回退
- ❌ **graphicview.cpp** - 部分修改但编译失败
- ❌ **statusLabel 清理** - 未完整执行

### 关键决策点
1. **第一次编译失败** → 选择 Git Checkout 而非深入调试
2. **尝试 DbcPanel 声明修复** → 成功添加到头文件
3. **最终回退** → 失去所有修改成果

---

## 🚨 警告

**当前代码库状态**: ⚠️ **不可用**

```bash
# 编译失败的典型错误:
error: 'm_statusLabel' was not declared in this scope
error: 'onImportDatabase' is not a member of 'DbcPanel'
FAILED: src/CMakeFiles/openbus_ui.dir/ui/panels/sidebarpanels.cpp.obj
```

**建议**: 
- 不要基于当前分支进行开发
- 需要先解决编译问题
- 考虑创建一个干净的 feature 分支重新开始

---

## 🔄 未来规划

### 推荐重构策略
1. **小步快跑** - 每次只改一个功能点
2. **编译验证** - 每步都确保能编译通过
3. **Git 提交** - 每步都建立 checkpoint
4. **优先级排序** - 先解决编译阻塞，再做 UI 优化

### 技术债清单
- [ ] m_statusLabel 完整清理
- [ ] TracePanel 右键菜单完整实现
- [ ] 验证现有功能是否正常
- [ ] 建立 CI/CD 自动化编译检查
- [ ] 编写关键功能的单元测试

---

## 📞 联系信息

**项目负责人**: [待填写]  
**技术负责人**: [待填写]  
**下次迭代日期**: [待确定]

---

*本文档由 AI Agent 生成 · 仅供参考 · 建议定期更新*
