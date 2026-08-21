# Graphic 离线回放中途添加信号鲁棒性优化方案

> 状态：**待评审**
> 创建：2026-08-21（第六轮，用户实测反馈）
> 关联：doc/离线分析ASC兼容与工程现场还原方案.md（第五轮，已实施）

## 1. 问题现象（用户实测描述）

离线文件分析时：

1. 点击"开始"后 Graphic 页面**有曲线显示**（回放进行中）；
2. **中途添加信号到 Graphic** 后，出现 **Graphic 没有波形显示** 的现象。

要求从**操作鲁棒性**入手优化。

## 2. 调研结论（附证据）

### 2.1 链路静态分析

"添加信号到 Graphic" 的全部四个入口：

| 入口 | 链路 | 性质 |
|------|------|------|
| DBC 页双击信号 | onSignalDoubleClicked → graphicInvoke("addSignal") → gv->addSignal | **增量** |
| Trace 页双击帧 | onFrameDoubleClicked → graphicInvoke("addSignal")（raw byte0 信号） | **增量** |
| Trace 右键"添加信号到 Graphic" | onFrameAddToGraphic → graphicInvoke("addSignals") | **增量** |
| Graphic 页 "+" 按钮（SignalConfigDialog） | gv->addSignal 直调 | **增量** |

`GraphicView::addSignal`（graphicview.cpp L1712）逐行核对：新建独立
axisRect/graph/nameLabel → X 轴对齐当前主视口 → append 到 m_signals →
layoutAxisRects 重布局（只动布局不动数据）。**rawData 与 graph 显示数据
均不被触碰**。

工程恢复路径 `loadSignalConfigs`（clearSignals + 重建）仅由
mainwindow_project.cpp 打开工程时调用，与回放中途添加无关。

### 2.2 复现测试（决定性证据）

新增 offscreen 用例 **UI-06 graphicReplayAddSignalKeepsWave**
（tests/test_ui_offscreen.cpp）：直接驱动 GraphicView::onFrame 模拟
回放帧流，分四段断言（displayedPointCount 为本次新增的只读诊断接口，
返回视口抽稀后 graph 显示点数，0 = 无波形）：

| 场景 | 断言 | 结果 |
|------|------|------|
| 1. 回放前半段（0..1s，1000 帧） | 信号 A 有波形（基线） | **PASS** |
| 2. 中途 addSignal(B) | 信号 A 波形保留 | **PASS** |
| 3. 后半段继续（1..2s，两 ID 并流） | A、B 都有波形 | **PASS** |
| 4. **回放结束后 addSignal(C)** | C 显示 **0 点**（缺陷基线）；A 不受影响 | PASS（记录缺陷） |

**结论：GraphicView 层添加信号是严格增量的，旧波形不会被清空**（场景
1-3）。用户感知的"波形消失"来自以下根因组合（场景 4 为实锤）。

### 2.3 根因分析

**根因 A（核心缺陷，UI-06 场景 4 实锤）：中途/结束后添加的信号没有
历史数据回填。**

离线回放的全部帧常驻 Player 内存（onMeasurementToggled 离线分支
loadFrames 全量加载），但添加信号只从"添加时刻"开始收数：

- 回放**接近尾声**时添加（大文件高速回放下常见）→ 新信号只收几帧甚至零帧；
- 回放**已结束/暂停**时添加 → 新信号**永远空白**；
- 新空轨道加入后布局重排、挤压既有轨道，用户视觉焦点落在新轨道上 →
  感知"Graphic 没有波形了"。

这与 CANoe 等工具的行为形成核心体验差距：CANoe 中离线回放下添加信号
立即显示完整历史曲线（数据源是静态文件，完全可以重扫）。

**根因 B（体验放大器）：想看新信号完整曲线的唯一途径是再次点"开始"**
→ onMeasurementToggled 触发 clearDataAll **清空全部视图数据**后重播。
当前分析现场（缩放位置、已看进度）被破坏；用户若误以为"添加信号导致清空"，
与现象描述吻合。

**根因 C（次要，防御项）：addSignal 的显示刷新不完整。**

- 分栏模式下 addSignal 末尾只 replot、不 refreshDisplayData（叠加模式
  经 applyYAxisMode 有）；
- primaryXAxis 为空时（全部轨道隐藏）新信号回退初始视口 [0, 30s]，若
  数据时间戳为绝对时间（BLF 常见）则视口错位到无数据区。

## 3. 修复方案

### F1（核心）：离线回放历史回填

数据源用 Player（零拷贝、零额外内存——QVector 隐式共享，只读不解耦）：

1. **Player**（core/player.h）：新增只读访问器
   `const QVector<CanFrame> &frames() const { return m_frames; }`
   （currentFrameIndex()/totalFrames() 已存在）。

2. **GraphicModule**（src/ui/graphicmodule.cpp）：
   - createPage 保存 `m_ctx.player = ctx.player`（现只存了 mainWindow/
     shellInvoke 等四个字段，player 一直被丢弃）；
   - invoke 的 "addSignal"/"addSignals" 两个分支：构造 gsig 后，若
     `m_ctx.player && m_ctx.player->isLoaded()`，取
     `history = &player->frames()`、`historyCount = player->currentFrameIndex()`
     （**已播放前缀**——不"剧透"未播数据），调
     `gv->addSignal(gsig, history, historyCount)`；
   - 实时采集模式（player 未加载）history 为 nullptr，行为与现状完全一致。

3. **GraphicView**（src/ui/graphicview.h/.cpp）：addSignal 增加可选参数
   ```cpp
   void addSignal(const Signal &sig,
                  const QVector<CanFrame> *history = nullptr,
                  int historyCount = -1);
   ```
   - 信号入列后若 history 非空：遍历前 historyCount 帧（-1 = 全部），
     仅对新信号做 ID/extended 匹配 + extractValue + pushSample（**不影响
     既有信号，无重复数据**）；
   - 回填后 `ensureMinMax` + Y 轴自适应（同 loadFile 既有逻辑）+
     `refreshDisplayData()`；
   - 语义：新信号立即显示"从回放开始到当前进度"的完整曲线。

   覆盖面说明：GraphicView 内部 "+" 按钮直调 addSignal（无 history），
   该路径回放中后续帧仍会流入新信号，暂不回填；模块转发的三个入口
   （DBC 双击/Trace 双击/Trace 右键）为用户离线分析主路径，全部覆盖。
   loadSignalConfigs（工程恢复）不回填——恢复现场语义下 Player 未加载。

### F2（防御加固）

1. **addSignal 末尾统一 `refreshDisplayData()`**：分栏模式补齐（叠加
   模式经 applyYAxisMode 已含），消除新信号显示数据与视口的潜在不同步。
2. **primaryXAxis 为空时的回退视口改为数据感知**：
   `[max(0, m_currentTime - m_timeWindow), max(m_timeWindow, m_currentTime)]`
   ——绝对时间戳文件下新信号初始视口不再错位到 [0, 30s] 无数据区。

### F3（测试）

1. UI-06 场景 4 断言翻转：`gv->addSignal(sigC, &frames, count)` 后
   `displayedPointCount(2) > 0`（回填生效）；
2. UI-06 追加：回放中途添加带 history 截断（count < 总帧数）→ 新信号
   只含已播前缀数据（不剧透）；
3. 既有 7 用例 + 第五轮全部 9 套件回归。

## 4. 影响面与风险

| 项 | 评估 |
|----|------|
| 实时采集模式 | player 未加载 → history = nullptr → 行为零变化 |
| 离线回放中途添加 | 新信号立即有历史曲线（目标行为）；旧信号零影响（回填只写新信号） |
| 回放暂停时添加 | currentFrameIndex 冻结在暂停位置 → 回填到暂停点，符合直觉 |
| 循环回放 | 循环重启时 currentFrameIndex 归 0 → 回填量随之减少，可接受 |
| 内存 | Player 帧数据已全量在内存，QVector 只读共享零拷贝；无新增缓存 |
| ABI | Player 新增 inline 访问器（openbus_data 重建，依赖方全量重链一次） |

## 5. 测试计划

- UI-06 扩展 + 断言翻转（见 F3）；
- ctest 9 套件全量回归；
- 手工验证（用户场景）：离线分析 → 开始 → 回放中 DBC 双击加信号 →
  新信号立即显示完整历史曲线；回放结束后再加 → 同样立即有完整曲线；
  再次点开始 → 重播现场与现状一致。

## 6. 实施清单

- [ ] F1-1 Player::frames() 访问器（core/player.h）
- [ ] F1-2 GraphicModule 保存 ctx.player + addSignal/addSignals 带 history 转发
- [ ] F1-3 GraphicView::addSignal 历史回填 + Y 轴自适应
- [ ] F2-1 addSignal 末尾统一 refreshDisplayData
- [ ] F2-2 primaryXAxis 空回退视口数据感知
- [ ] F3 测试断言翻转 + 中途截断用例
- [ ] 全量回归 + 测试报告 §9/§10 更新
