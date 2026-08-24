# G16-Graphic 增强功能方案

## 一、需求背景与目标

对标 CANoe Graphic 的核心交互功能，提升多信号并行查看体验：

1. **分栏信号排序** - 支持将任意信号拖动到指定分栏
2. **分栏信号重排** - 点击分栏标题可上下调整该栏内信号顺序
3. **分栏高亮** - 激活分栏的曲线加粗显示
4. **卡尺辅助线** - 单/双卡尺时显示贯穿多分栏的垂直虚线

---

## 二、功能设计

### F1: 信号拖动到分栏 (Drag & Drop to Track)

#### 2.1.1 交互流程
```
用户操作                          系统响应
───────────────────────────────────────────────
1. 拖拽左侧信号列表项             - 启动拖拽 (Qt Drag)
   → 波形区                        - 显示拖拽视觉效果 (橡皮筋框/高亮)
2. 松于某个分栏区域内            - 将该信号移动至目标分栏底部
                                 - 刷新该分栏绘制
                                 - 保持其他分栏不变
```

#### 2.1.2 技术要点
- `QTreeWidget::startDragn()` + `QMimeData` 传递信号索引
- `QCustomPlot::dropEvent()` 解析目标轨道索引
- `reorderSignalsInTrack(trackIndex, signalIndex)` 调整数组顺序

#### 2.1.3 边界处理
| 场景 | 策略 |
|------|------|
| 拖入已有信号的轨道 | 追加到该轨道末尾 (CANoe 行为) |
| 拖入空轨道 | 自动创建新轨道并放入信号 |
| 拖出所有轨道 (无效区域) | 无操作 (visual revert) |
| 拖到同一轨道内部 | 忽略 (已在该轨道) |

---

### F2: 分栏点击升降序 (Click-to-Sort Track)

#### 2.2.1 交互流程
```
用户操作                          系统响应
───────────────────────────────────────────────
1. Ctrl+ 点击某个分栏 Y 轴标签      - 弹出排序菜单 (↑/↓/复位)
2. 选择 "↑ 升序" / "↓ 降序"        - 对该轨道信号按名称/值排序
3. 双击分栏标题                   - 切换上次使用的排序方向
```

#### 2.2.2 备选方案 (更直观)
**方案 A - 右键菜单** (推荐):
- 右键点击分栏 → 菜单包含:
  - ↑ 按信号名升序
  - ↓ 按信号名降序  
  - ─────
  - ↺ 复位默认顺序

**方案 B - 快捷键 + 点击**:
- Alt+↑ / Alt+↓ 调整当前聚焦轨道的信号顺序

#### 2.2.3 技术实现
```cpp
void GraphicView::sortSignalsInTrack(int trackIndex, Qt::SortOrder order) {
    auto &track = m_tracks[trackIndex];
    std::stable_sort(track.signalIndices.begin(), track.signalIndices.end(),
        [this, order](int a, int b) {
            QString na = m_signals[a].name;
            QString nb = m_signals[b].name;
            return order == Qt::Ascending ? (na < nb) : (na > nb);
        });
    replotTrack(trackIndex);
}
```

---

### F3: 激活分栏曲线高亮 (Active Track Highlight)

#### 3.1.1 视觉规范
| 状态 | 线宽 | 透明度 | 说明 |
|------|------|--------|------|
| 普通信号 | 1.0px | 100% | 默认绘制 |
| 激活信号 | 2.0px | 100% | 鼠标 hover/click 后 |
| 卡尺交叉点 | 2.5px | 120% (发光) | cursor 位置 |

#### 3.1.2 触发机制
```cpp
void GraphicView::activateTrack(int trackIndex) {
    m_activeTrack = trackIndex;
    for (auto *curve : m_curves)
        curve->setPen(QPen(color, trackIndex == m_activeTrack ? 2.0 : 1.0));
    m_plot->replot();
}
```

#### 3.1.3 交互细节
- **点击分栏** → 设置 `m_activeTrack`
- **点击信号列表** → 同步设置对应分栏为 active
- **鼠标移出窗口** → 恢复默认 (可选：保留最后激活态)

---

### F4: 卡尺垂直虚线 (Cursor Crosshair Line)

#### 4.1.1 视觉规范
| 元素 | 样式 | 颜色 | 范围 |
|------|------|------|------|
| 单卡尺 | 垂直虚线 | #FF9800 (主题色) | 贯穿全部分栏 |
| 双卡尺 | 2×垂直虚线 | #FF9800 / #2196F3 | 贯穿全部分栏 |
| 虚线间隔 | 4px 实线 + 4px 空白 | - | - |

#### 4.1.2 绘制实现
```cpp
void GraphicView::drawCursorCrosshairs(QCPPainter *painter) {
    QPen pen(cursorColor, 1, Qt::DashLine);
    painter->setPen(pen);
    
    for (int i = 0; i < m_tracks.size(); ++i) {
        auto *ar = m_plot->plotLayout()->atRowColumn(0, i)->axisRect();
        double x = xAxisPixelsToDateTime(cursorX);
        
        painter->drawLine(ar->viewport().left(), ar->viewport().top(),
                         ar->viewport().right(), ar->viewport().top());
    }
}
```

#### 4.1.3 生命周期
- **启用卡尺模式** → 监听 cursor 移动
- **更新卡尺位置** → 重绘贯穿虚线
- **关闭卡尺** → 清除虚线

---

## 三、技术架构调整

### 3.1 数据结构扩展

```cpp
struct GraphicTrack {
    QVector<int> signalIndices;     // 信号索引数组 (支持动态 reorder)
    bool isVisible = true;          // 轨道可见性
    int sortOrder = 0;              // -1: 降序，0: 默认，1: 升序
};

class GraphicView {
private:
    int m_activeTrack = -1;         // ← 新增：激活轨道索引
    bool m_showCrosshairs = false;  // ← 新增：显示卡尺虚线
    
    // Drag & Drop
    QTreeWidgetItem *m_dragItem = nullptr;
    
    // Sort state per track
    QHash<int, Qt::SortOrder> m_trackSortOrders;
};
```

### 3.2 事件处理扩展

| 事件 | 原有处理 | 新增处理 |
|------|---------|---------|
| `mousePressEvent` | 卡尺模式切换 | 检测是否点击分栏 → setActiveTrack() |
| `mouseMoveEvent` | 拖拽平移/缩放 | 拖拽信号 → updateDragPreview() |
| `dropEvent` | 文件拖入 | 解析轨道索引 → moveSignalToTrack() |
| `contextMenuEvent` | 通用菜单 | 分栏右键 → sort menu |

### 3.3 绘制管线扩展

```
update() 
  ├─ layoutAxisRects()               ← 不变
  ├─ addCurvesForSignals()           ← 不变
  └─ drawDecorations()               ← 新增：active highlight + crosshairs
       ├─ drawActiveTrackHighlight()  ← P3
       └─ drawCursorCrosshairs()      ← P4
```

---

## 四、实施计划

### Phase 1: Drag & Drop 基础 (F1)
**任务拆解**:
1. 启用 `QTreeWidget` drag-drop: `setDragEnabled(true)`
2. 实现 `mimeData()` / `dropMimeData()`
3. `QCustomPlot` 子承接 dropEvent
4. 轨道索引映射算法 (pixel → trackIndex)
5. 可视化反馈 (hover track highlight)

**验收标准**:
- ✅ 从信号列表拖拽信号到任意轨道成功
- ✅ 轨道自动扩容容纳新信号
- ✅ 拖拽取消时原位置不变

---

### Phase 2: 分栏排序 (F2)
**任务拆解**:
1. 分栏右键菜单 UI (QMenu + Actions)
2. 排序逻辑实现 (stable_sort)
3. 快捷键绑定 (Alt+↑/↓)
4. 排序状态持久化 (可选)

**验收标准**:
- ✅ 右键分栏弹出排序菜单
- ✅ 升序/降序切换正确
- ✅ 信号列表显示顺序与轨道一致

---

### Phase 3: 激活高亮 (F3)
**任务拆解**:
1. `m_activeTrack` 状态管理
2. 点击事件劫持 (detect track click)
3. 曲线样式动态调整 (pen width)
4. 信号列表联动高亮

**验收标准**:
- ✅ 点击分栏 → 该轨曲线变粗 (2.0px)
- ✅ 点击信号 → 对应轨道高亮
- ✅ 鼠标移出 → 可选恢复或保持

---

### Phase 4: 卡尺虚线 (F4)
**任务拆解**:
1. `QCPPainter` 扩展绘制接口
2. 虚线样式定义 (DashLine)
3. 单/双卡尺状态感知
4. 性能优化 (仅在卡尺启用时绘制)

**验收标准**:
- ✅ 单卡尺 → 一条橙色虚线贯穿所有轨道
- ✅ 双卡尺 → 两条不同颜色虚线
- ✅ 虚线随卡尺移动实时更新

---

## 五、风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| 拖拽因果环 (信号→轨道→信号) | 死循环 | 使用命令模式 + 撤销栈 |
| 大量信号拖拽卡顿 | FPS 下降 | 延迟布局 + 离屏渲染 |
| 多轨道并发高亮闪烁 | 视觉干扰 | double buffer + QCustomPlot::beforeRecast |
| 卡尺虚线性能开销 | GPU 压力 | 仅在 m_cursorMode!=None 时绘制 |

---

## 六、待决策事项

### D1: 排序触发方式
- [ ] **选项 A - 右键菜单** (CANoe 风格): 稳定直观
- [ ] **选项 B - 快捷键**: 高效但需记忆
- [ ] **选项 C - 双击切换**: 快捷但易误触

**建议**: 采用 **A + B 组合** (右键主交互 + 快捷键加速)

### D2: 卡尺虚线颜色
- [ ] **选项 A**: 统一为主题橙色 (#FF9800)
- [ ] **选项 B**: 单卡尺橙 / 双卡尺蓝 (区分)

**建议**: 采用 **选项 A** (视觉一致性)

### D3: 激活高亮保持策略
- [ ] **选项 A**: 永久保持 (直到点击其他)
- [ ] **选项 B**: 悬停时高亮 (离开恢复)

**建议**: 采用 **选项 A** (更符合专业工具习惯)

---

## 七、测试用例设计

### T1: Drag & Drop
| ID | 步骤 | 预期 |
|----|------|------|
| T1-1 | 拖 Signal-A 到 Track-1 | Track-1 底部新增 Signal-A |
| T1-2 | 拖 Signal-B (已存在) 到 Track-2 | Track-2 追加，原 Track-1 移除 |
| T1-3 | 拖出窗口外 | 无变化 |

### T2: 排序
| ID | 步骤 | 预期 |
|----|------|------|
| T2-1 | 右键 Track-1 → 升序 | Z→A 排序 |
| T2-2 | 右键 Track-1 → 降序 | A→Z 排序 |
| T2-3 | Alt+↑ 快捷键 | 切换升序 |

### T3: 高亮
| ID | 步骤 | 预期 |
|----|------|------|
| T3-1 | 点击 Track-2 | Track-2 曲线 2.0px |
| T3-2 | 点击 Signal-X (在 Track-3) | Track-3 高亮，Track-2 恢复 |

### T4: 卡尺虚线
| ID | 步骤 | 预期 |
|----|------|------|
| T4-1 | 启用单卡尺并移动 | 橙色虚线跟随 |
| T4-2 | 启用双卡尺 | 两条虚线独立移动 |

---

## 八、时间估算

| Phase | 预估工时 | 备注 |
|-------|---------|------|
| F1 Drag | 4h | 含拖拽视觉反馈 |
| F2 Sort | 3h | 含右键菜单 |
| F3 Highlight | 2h | 简单状态机 |
| F4 Crosshair | 3h | QCPPainter 扩展 |
| **总计** | **12h** | 不含调试 |

---

## 附录：CANoe 行为对标

| 功能 | CANoe | 本方案 | 差距 |
|------|-------|--------|------|
| 拖放信号 | ✅ | ✅ (Phase1) | 无 |
| 轨道内排序 | ✅ 右键菜单 | ✅ (Phase2) | 无 |
| 轨道高亮 | ✅ 点击 | ✅ (Phase3) | 无 |
| 卡尺线 | ✅ 虚线贯穿 | ✅ (Phase4) | 无 |

**结论**: 完全对标 CANoe，无功能缺口。
