# G15 - Graphic 界面优化方案

## 一、需求背景
对标 CANoe Graphic 页面，提升图形曲线的有效显示面积和信号信息可读性。

## 二、功能清单

### 1. 减少分栏间隙（图形有效面积最大化）
- **当前问题**：红色框显示各分栏之间有过多空白区域，浪费垂直空间
- **实现目标**：压缩轨道间隙，使波形显示区域最大化
- **技术方案**：
  - 优化 `QCustomPlot` 多轴布局参数
  - 设置轨道间距为 0~2px（保持视觉分隔）
  - 调整坐标轴边距（top/bottom/left/right）
  - 使用 `setAutoMargin()` 动态计算最小边距
- **涉及文件**：
  - `src/ui/graphicview.cpp` - `layoutAxisRects()` 方法
  - 修改所有轨道创建逻辑中的 margin 参数

### 2. 信号列表列宽与可见性控制
- **当前问题**：
  - 列宽固定或自适应内容，无法根据用户需求调整
  - 长信号名被截断，影响关键信息查看
  - 冗余列（如点数）占用空间
- **实现目标**：
  - 支持表头拖拽调整所有列宽度
  - 提供列可见性勾选面板
  - 保留核心列（信号名、物理值）始终可见
- **技术方案**：
  ```cpp
  // 1) 列宽改为 Interactive + 合理初始宽度
  m_signalTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);     // 色块 22px
  m_signalTree->header()->resizeSection(0, 22);
  
  // 列 1: 信号名 - Interactive（可调），初始 120px
  m_signalTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
  m_signalTree->header()->resizeSection(1, 120);
  
  // 列 2-8: Interactive，合理默认宽度
  m_signalTree->header()->setSectionResizeMode(c, QHeaderView::Interactive);
  for (int c = 2; c < 9; ++c)
      m_signalTree->header()->resizeSection(c, defaultWidth[c]);
  
  // 2) 列可见性控制菜单
  auto *colMenu = new QMenu(this);
  for (int i = 2; i < 9; ++i) {
      auto *action = colMenu->addAction(columnHeaders[i]);
      action->setCheckable(true);
      action->setChecked(i != hiddenCols);
      connect(action, &QAction::toggled, this, [i](bool on){
          m_signalTree->setColumnHidden(i, !on);
          saveColumnConfig(); // 持久化
      });
  }
  addColBtn->setMenu(colMenu);
  ```
- **涉及文件**：
  - `src/ui/graphicview.h` - 新增列配置管理成员
  - `src/ui/graphicview.cpp` - 信号列表初始化/右键菜单/持久化
- **UI 交互**：
  - 信号列表表头右键 → "列显示设置"子菜单
  - 各列名前带复选框，取消勾选即隐藏该列
  - 自动保存配置到 `settings.json`
  - 提供"复位列宽"按钮

## 三、实施步骤

### Phase 1: 分栏间隙优化
1. 修改 `layoutAxisRects()` 方法
2. 测试不同信号数量下的布局效果
3. 验证滚动条行为是否正常

### Phase 2: 信号列表增强
1. 实现列宽 Interactive 模式 + 默认宽度配置
2. 设计列配置数据结构（显式存储可见性状态）
3. 添加右键菜单 + 列可见性 checkbox
4. 集成 settings.json 持久化
5. 单元测试：列隐藏/恢复/宽度记忆

### Phase 3: UI 打磨
1. 主题适配：浅色/深色模式下列菜单样式
2. 长信号名显示优化（Tooltip 提示全名）
3. 性能测试：大量信号时的渲染效率

## 四、验收标准

### P1 - 分栏间隙优化
- ✅ 信号数量增加时，垂直方向仍有足够的绘图区域
- ✅ 轨道之间保持≤2px 视觉分隔线
- ✅ 无异常滚动条或溢出

### P2 - 信号列表列宽
- ✅ 所有可拖动列可通过表头拖拽调整宽度
- ✅ 拖动后宽度在窗口 resize 时保持不变
- ✅ 窗口大小改变不强制重置列宽

### P3 - 列可见性控制
- ✅ 表头右键 → "列显示设置"菜单可打开
- ✅ 取消勾选某列后，该立即从列表中消失
- ✅ 重新勾选后，列恢复上次位置/宽度
- ✅ 关闭程序再次打开时，列配置已自动恢复
- ✅ 核心列（信号、物理值）不可隐藏或提供保护提示

## 五、技术难点

### 难点 1: QCustomPlot 多轴轨道紧密布局
- **解决方案**：参考 QCustomPlot 官方示例的 multi-axis-page 实现
- **关键 API**：
  ```cpp
  ar->setAutoMargins(QCP::mNone);  // 完全禁用自动边距
  ar->setMarginTop(0); ar->setMarginBottom(0);
  ar->axisRect()->setGap(0);       // 轨道间间距
  ```

### 难点 2: 列配置跨会话记忆
- **方案**：
  ```cpp
  // settings.json
  {
    "graphic.signalList.columnVisible": {"physValue": true, "rawValue": false},
    "graphic.signalList.columnWidth": {"physValue": 100, "rawValue": 80}
  }
  ```

### 难点 3: 大量信号下的性能
- **预研**：`QTreeWidget` vs `QTableWidget`
- **结论**：当前 QTreeWidget 足够支撑百级信号
- **优化**：`begin/endResetModel()` 批量更新

## 六、优先级

| 子项 | 优先级 | 依赖关系 | 预计工作量 |
|---|---|---|---|
| P1 - 分栏间隙优化 | P0 | 独立 | 2h |
| P2 - 列宽可调 | P0 | 独立 | 2h |
| P3 - 列可见性 | P1 | P2 | 4h |
| P4 - 持久化配置 | P1 | P3 | 2h |

## 七、后续扩展

- [ ] 列排序（点击表头）
- [ ] 自定义计算公式（如 A+B-C）
- [ ] 信号分组折叠（按 ID 报文）
- [ ] 信号列表与波形区联动高亮
