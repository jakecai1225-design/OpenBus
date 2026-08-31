# SendFlow 流指示器系统 - M1 功能实现说明

## 🎯 概述

SendFlow 模块的流指示器系统（M1）提供了直观的视觉反馈，通过状态灯机制实时显示 CAN 数据发送流程的健康状态。

## 💡 设计理念

参考 FlowModule (measurementsetupview) 的实现，采用**状态灯**模式提供以下四种视觉反馈：

### 状态灯定义

| 状态 | 灯光行为 | 含义 |
|------|---------|------|
| **Off** | 不亮灯（灰色） | 未使能/未运行 |
| **On** | 常亮绿色 | 待命状态（enabled 但暂无数据流） |
| **Blink** | 闪烁绿色 | 数据流活跃（有 Tx/Rx 帧到达） |
| **Error Blink** | 闪烁红色 | 运行异常/设备断开 |

## 🏗️ 架构实现

### 数据结构

```cpp
// BlockItem 新增状态灯成员
struct BlockItem {
    // ... existing fields ...
    
    // M1: 状态灯系统
    BlockLamp lamp = BlockLamp::Off;     ///< 当前指示灯状态
    bool lampBlinkPhase = false;         ///< 闪烁相位（true=亮，false=灭）
};

enum class BlockLamp {
    Off,      ///< 不亮灯（未使能）
    On,       ///< 常亮绿色（待命）
    BlinkOn,  ///< 闪烁亮相（数据流活跃或异常）
    BlinkOff  ///< 闪烁灭相
};
```

### 定时器系统

```cpp
QTimer *m_blinkTimer = nullptr;  ///< 灯闪烁相位驱动（500ms，仅有流/异常时运转）
bool m_blinkOn = false;          ///< 当前闪烁相位（true = 亮）
qint64 m_lastFrameMs = 0;        ///< 最近一帧到达时刻（数据流活跃判定）
QSet<QString> m_blockErrors;     ///< 处于异常态的块 ID 集合
```

**闪烁周期**: 500ms（与 FlowModule 一致）

## 🔧 关键方法

### 1. `onFrameReceived(const CanFrame &frame)`

**触发时机**: 
- 信号发送 Tab 发送 Tx 帧时调用
- 任何从总线接收的 Rx 帧也可调用

**作用**:
- 更新 `m_lastFrameMs`
- 启动闪烁定时器（如果已运行且无超时）
- 检查是否应继续闪烁（2 秒无帧则停止）

```cpp
void SendFlowView::onFrameReceived(const class CanFrame &frame)
{
    Q_UNUSED(frame)
    
    m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    
    // 启动闪烁定时器（如果尚未运行）
    if (!m_blinkTimer->isActive() && m_running) {
        startBlinkTimer();
    }
    
    checkFlowActivity();
}
```

### 2. `updateBlockLamps()`

**全量刷新所有块的状态灯**，根据以下条件确定每个块的显示：

1. **块是否启用** (`enabled`): 未启用 → Off
2. **是否有错误** (`m_blockErrors.contains(blockId)`): 有错误 → Red Blink
3. **数据流是否活跃** (`isFlowActiveForBlock`): 
   - 活跃 → Green Blink
   - 否则（正在运行）→ Green On（待命）
4. **是否正在运行** (`m_running`): 未运行 → Off

```cpp
void SendFlowView::updateBlockLamps()
{
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        BlockItem &block = it.value();
        
        // 确定当前块应显示的状态灯
        BlockLamp lamp = BlockLamp::Off;
        bool blinkPhase = block.lampBlinkPhase;
        
        if (!block.enabled) {
            lamp = BlockLamp::Off;
        } else if (m_blockErrors.contains(block.id)) {
            lamp = BlockLamp::BlinkOn;
            blinkPhase = m_blinkOn;
        } else if (m_running && isFlowActiveForBlock(block.id)) {
            lamp = BlockLamp::BlinkOn;
            blinkPhase = m_blinkOn;
        } else if (m_running) {
            lamp = BlockLamp::On;
            blinkPhase = false;
        }
        
        block.lamp = lamp;
        block.lampBlinkPhase = blinkPhase;
        
        if (block.gfxItem) {
            drawBlockLamp(block.gfxItem, lamp, blinkPhase);
        }
    }
}
```

### 3. `drawBlockLamp()`

**绘制单个块的状态灯**，位于块右上角的小圆点：

```cpp
void SendFlowView::drawBlockLamp(QGraphicsRectItem *gfxItem, BlockLamp lamp, bool blinkPhase)
{
    QPainter painter(gfxItem);
    QRectF rect = gfxItem->boundingRect();
    
    int lampRadius = 8;
    QPoint lampPos(rect.right() - lampRadius - 5,
                   rect.top() + lampRadius + 5);
    QRectF lampRect(lampPos.x() - lampRadius,
                    lampPos.y() - lampRadius,
                    lampRadius * 2, lampRadius * 2);
    
    // 根据状态选择颜色
    QColor lampColor;
    bool isErrorResponse = m_blockErrors.contains(gfxItem->data(0).toString());
    
    if (blinkPhase && lamp == BlockLamp::BlinkOn) {
        if (isErrorResponse) {
            lampColor = QColor("#f44336");  // 红色闪烁亮
        } else {
            lampColor = QColor("#4CAF50");  // 绿色闪烁亮
        }
    } else if (lamp == BlockLamp::On) {
        lampColor = QColor("#4CAF50");  // 常亮绿色
    } else {
        lampColor = QColor("#9e9e9e");  // 灰暗（灭）
    }
    
    // 绘制发光效果
    QRadialGradient gradient(lampRect.center(), lampRadius);
    if (blinkPhase && lamp == BlockLamp::BlinkOn) {
        gradient.setColorAt(0.0, lampColor.lighter(150));  // 发光外圈
        gradient.setColorAt(0.6, lampColor);
        gradient.setColorAt(1.0, lampColor.darker(150));
    } else {
        gradient.setColorAt(0.0, lampColor);
        gradient.setColorAt(1.0, lampColor.darker(150));
    }
    
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(lampRect);
}
```

## 📊 使用场景

### 场景 1: 开始发送

1. 用户在收发模块加载配置并开始周期发送
2. 调用 `view->onFrameReceived(frame)`（需从 SignalSendTab 集成）
3. 状态变化:
   - **SignalGenerator**: Off → Green On (待命) → **Green Blink** (活跃)
   - **Trace/Graphic/Record**: Off → Green On (待命) → **Green Blink** (活跃)
   - **Real Device**: On (常亮表示已连接)

### 场景 2: 停止发送

1. 用户停止发送或关闭收发模块
2. 超过 2 秒无帧到达 → `checkFlowActivity()` 触发
3. 状态变化:
   - `stopBlinkTimer()` 停止闪烁
   - **所有模块**: Green Blink → **Green On** (待命)
   - 如果测量停止 (`setRunning(false)`): → **Off** (灰化)

### 场景 3: 设备断开

1. 物理设备被拔出或驱动异常
2. `CanDeviceManager::connectionChanged(false, deviceName)` 触发
3. `updateDeviceStatus()` 检测到断开
4. `setBlockError("real_device", true)` 设置错误状态
5. **Real Device**: Green On → **Red Blink** (错误)

## 🎨 视觉效果

### 状态灯位置
- **位置**: 块右上角
- **半径**: 8px
- **间距**: 距离边界 5px

### 发光效果

```
BlinkOn (亮相):  ┌──────┐
                 │ ○═══○ │  <- 外圈发光明亮
                 │ ╭─●─╮ │  <- 中心实心
                 │ ╰───╯ │
                 └──────┘

BlinkOn (灭相):  ┌──────┐
                 │      │
                 │       │  <- 中心稍暗
                 │        │
                 └──────┘

On (常亮):       ┌──────┐
                 │ ●══●│  <- 稳定的半透明光晕
                 │ ──●──│
                 └──────┘

Off (熄灭):      ┌──────┐
                 │      │
                 │       │  <- 深灰色小点
                 │        │
                 └──────┘
```

## 🔗 与其他组件的交互

### SignalSendTab ↔ SendFlowView

需在 `transceivemodule.cpp` 中增加回调：

```cpp
// 在 sendRowRequested 的连接中
QObject::connect(timer, &QTimer::timeout, tab,
    [this, id, data, row, count, timer, remaining, &injectTxLoopback](...) {
    CanFrame f;
    f.id = id;
    f.data = data;
    f.direction = CanFrame::Tx;
    
    if (m_ctx.deviceManager) {
        CanFrame echo;
        if (m_ctx.deviceManager->sendFrame(f, &echo)) {
            injectTxLoopback(echo);
            
            // M1: 通知 SendFlow 数据流活跃
            if (m_ctx.shellInvoke) {
                m_ctx.shellInvoke(QStringLiteral("notifyFrameSent"), QVariantList() << echo);
            }
        }
    }
    // ... rest of code
});
```

### ShellContext 新增动作

```cpp
// MainWindow 中添加处理
"notifyFrameSent" (arg = QVariantList{CanFrame frame})
    -> onFrameReceived(frame)

"setSendFlowRunning" (arg = bool running)
    -> view->setRunning(running)
```

## 🛠️ 调试建议

### 验证闪烁功能

```cpp
// 在测试代码中手动触发
view->onFrameReceived(fakeFrame);
view->updateBlockLamps();  // 立即刷新
view->repaint();           // 强制重绘
```

### 性能监控

- 定时器仅在活跃时运行（节省 CPU）
- 使用 `MinimalViewportUpdate` 优化重绘
- 只更新变更的块（增量更新）

## 🚀 后续扩展

### M2: 精确到每个模块的数据流统计

```cpp
QMap<QString, qint64> m_moduleLastFrameMs;  // 每个模块独立的最后帧时间
```

### M3: 颜色主题适配

```cpp
QVariant themeData = QGuiApplication::style()->palette();
// 使用系统主题色动态调整状态灯
```

### M4: 点击状态灯快速诊断

```cpp
void mouseDoubleClickEvent(QMouseEvent *event) override {
    auto block = blockAt(m_view->mapToScene(event->pos()));
    if (block && block->lamp == BlockLamp::BlinkOn) {
        emit showStatistics(block->id);
    }
}
```
