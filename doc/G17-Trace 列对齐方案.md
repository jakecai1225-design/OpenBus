# G17-Trace 列对齐方案

## 一、需求分析

### 1.1 当前状态
- **问题**: Trace 页面所有列固定使用数据模型 (`CanTraceModel`) 定义的硬编码对齐方式，无法动态调整
- **位置**: `data()` 方法中的 `Qt::TextAlignmentRole` 分支 (cantracemodel.cpp:59-69)

### 1.2 用户需求
- 支持**左对齐**、**居中对齐**、**右对齐**三种模式
- 默认情况下所有列保持现有对齐（数字右对齐、文本左对齐）
- 用户可右键点击表头，为各列选择对齐方式
- 对齐配置需**持久化保存**到 `settings.json`

---

## 二、技术方案设计

### 2.1 数据结构扩展

#### 2.1.1 CanTraceModel 新增字段
```cpp
class CanTraceModel {
private:
    QHash<int, Qt::Alignment> m_columnAlignments;  // 列号 → 对齐方式
    // 默认值：通过 static const map 提供 fallback
    
    /// 获取列的有效对齐（用户定义 > 默认）
    Qt::Alignment effectiveAlignment(int column) const;
};
```

#### 2.1.2 默认对齐表
| 列名 | 当前对齐 | 推荐默认 | 理由 |
|------|---------|---------|------|
| No. | 右对齐 | 右对齐 | 序列号，数字语义 |
| Time | 右对齐 | 右对齐 | 时间戳 |
| Delta | 右对齐 | 右对齐 | 时间差 |
| Ch | 右对齐 | 右对齐 | 通道号 |
| Dir | 居中对齐 | 居中对齐 | Rx/Tx 短文本 |
| ID | 右对齐 | 右对齐 | 十六进制/十进制 |
| Name | 左对齐 | 左对齐 | 字符串长度不一 |
| DLC | 右对齐 | 右对齐 | 数字 |
| Data | 左对齐 | 左对齐 | HEX 字符串（类似 Hex 编辑器） |
| Flags | 居中对齐 | 居中对齐 | 标签如 `[ERR]` |
| Count | 右对齐 | 右对齐 | 计数器 |
| Signal | 左对齐 | 左对齐 | 信号表达式文本 |

### 2.2 UI 交互设计

#### 2.2.1 右键菜单项
```
┌─────────────────────────────┐
│ 对齐方式                      │
│ ○ 左对齐 (←)                 │
│ ● 居中对齐 (↔)               │ ← 当前选中
│ ○ 右对齐 (→)                 │
├─────────────────────────────┤
│ ↺ 恢复默认对齐               │
└─────────────────────────────┘
```

#### 2.2.2 Action 绑定
```cpp
connect(menu.addAction("左对齐"), &QAction::triggered, [this]() {
    setColumnAlignment(m_targetCol, Qt::AlignLeft);
});

// 同理 "居中对齐" / "右对齐"

// 分隔符 + 恢复默认
menu.addSeparator();
auto *reset = menu.addAction("↺ 恢复默认对齐");
connect(reset, &QAction::triggered, this, [this]() {
    m_columnAlignments.remove(m_targetCol);
    persistColumnAlignments();
    header()->updateSection(m_targetCol);
});
```

### 2.3 持久化方案

#### 2.3.1 QSettings 路径
```ini
[TraceLayout]
layout_version=3
col_0_alignment=4162       ; 2^13 = AlignRight + AlignVCenter
col_1_alignment=4162
col_2_alignment=4162
...
```

#### 2.3.2 合并逻辑
```cpp
void TraceView::restoreColumnLayout() {
    for (int c = 0; c < ColCount; ++c) {
        QString prefix = QStringLiteral("col_%1").arg(c);
        if (settings.contains(prefix + "_alignment")) {
            int alignInt = settings.value(prefix + "_alignment").toInt();
            Qt::Alignment align = static_cast<Qt::Alignment>(alignInt);
            model()->setColumnAlignment(c, align);
        }
    }
}
```

---

## 三、实施计划

### Phase 1: 核心模型改造 (CanTraceModel)

#### P1-1: 添加对齐存储
- [ ] 声明 `m_columnAlignments`
- [ ] 实现 `effectiveAlignment(column)`
- [ ] 更新 `data()` 方法返回用户自定义对齐

**验收标准**:
- ✅ 默认行为保持不变（所有列按原规则显示）
- ✅ 调用 `setColumnAlignment(col, align)` 后，下一帧渲染生效

#### P1-2: API 暴露
- [ ] `void setColumnAlignment(int col, Qt::Alignment align)`
- [ ] `Qt::Alignment columnAlignment(int col) const`
- [ ] `void resetToDefault(int col)`

---

### Phase 2: UI 交互 (TraceView)

#### P2-1: 右键菜单集成
- [ ] 监听 `horizontalHeader()->customContextMenuRequested`
- [ ] 记录点击的列号 (`m_contextMenuCol`)
- [ ] 动态构建对齐子菜单

#### P2-2: 恢复默认按钮
- [ ] 为每列独立提供 "重置" Action
- [ ] 清除本地缓存并触发 `header()->headerData()` 刷新

---

### Phase 3: 持久化

#### P3-1: 保存
- [ ] 在 `saveColumnLayout()` 中添加 `_alignment` 键
- [ ] `int(Qt::AlignRight | Qt::AlignVCenter)` 等枚举转 int

#### P3-2: 加载
- [ ] `restoreColumnLayout()` 优先读取对齐配置
- [ ] 初始化时调用 `model()->setColumnAlignment()`

---

## 四、技术风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| `Qt::Alignment` enum 版本兼容 | 跨平台编译失败 | 强制转 `int` 存储，加载时静态转换 |
| 头部 HeaderData 未刷新 | UI 不响应 | 显式调用 `horizontalHeader()->updateSection(col)` |
| 缓存导致对齐不变 | 测试失效 | 缓存淘汰 (`invalidateRowCache()`) + replot |

---

## 五、测试用例

### T1: 基础功能
| ID | 步骤 | 预期 |
|----|------|------|
| T1-1 | 右键 Data 列 → 右对齐 | 该列 HEX 数据右对齐，其他列不变 |
| T1-2 | 右键 Name 列 → 居中对齐 | "Name" 表头居中对齐 |
| T1-3 | 关闭程序重启 | 对齐配置保留 |

### T2: 边缘场景
| ID | 步骤 | 预期 |
|----|------|------|
| T2-1 | 无 DBC 时修改列宽 | 不影响对齐配置 |
| T2-2 | 切换颜色主题 | 对齐配置保持 |
| T2-3 | 插入/删除列（未来功能） | 已配置列不受影响 |

---

## 六、时间估算

| Phase | 预估工时 | 备注 |
|-------|---------|------|
| Model | 2h | 新增哈希表 + API |
| UI Menu | 2h | 右键菜单集成 |
| Persistence | 1h | save/load |
| **总计** | **5h** | 含调试 |

---

## 七、依赖关系

- **无外部依赖**: 纯内部改造，无需第三方库
- **向后兼容**: 旧配置文件缺失对齐字段 → 使用默认值
- **UI 版本控制**: `layout_version=3` → `layout_version=4`

---

## 八、待决策事项

### D1: 对齐枚举是否包含垂直？
- [ ] **选项 A**: 仅水平 (`AlignLeft/Center/Right`)
- [ ] **选项 B**: 保留垂直 (`AlignTop/Bottom/VCenter`)

**建议**: 采用 **选项 A**（垂直始终 VCenter，避免破坏表格行高）

### D2: 恢复默认是单列还是全局？
- [ ] **选项 A**: 每个列独立重置
- [ ] **选项 B**: 一键恢复所有列

**建议**: 采用 **选项 A**（粒度更细，避免误操作）
