#include "sendflowview.h"

#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QPainter>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <QTimer>

#include "core/candevicemanager.h"
#include "core/dbcmanager.h"

SendFlowView::SendFlowView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    
    // M1: 初始化闪烁定时器（500ms 周期）
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);  // 500ms 闪烁周期
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkOn = !m_blinkOn;  // 切换相位
        updateBlockLamps();  // 刷新所有块的状态灯
    });
}

void SendFlowView::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 画布区域
    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(QColor("#f5f5f5"));

    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::NoDrag);
    m_view->setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);

    mainLayout->addWidget(m_view);
    
    // M1: 连接设备管理器状态变化信号（如果已设置）
    if (m_deviceManager) {
        connect(m_deviceManager, &CanDeviceManager::connectionChanged,
                this, [this](bool connected, const QString &deviceName) {
            updateDeviceStatus();
        });
    }
}

void SendFlowView::buildTopology()
{
    initBlocks();
    drawConnections();
}

void SendFlowView::initBlocks()
{
    const int blockWidth = 160;
    const int blockHeight = 60;
    const int layerSpacing = 120;
    const int moduleSpacing = 180;

    int sceneWidth = static_cast<int>(m_scene->sceneRect().width());
    int sceneHeight = static_cast<int>(m_scene->sceneRect().height());
    m_scene->setSceneRect(0, 0, sceneWidth, sceneHeight);

    // Layer 1: SignalGenerator
    {
        BlockItem bg;
        bg.id = QStringLiteral("signal_generator");
        bg.title = QStringLiteral("信号发生器");  // 移除"CAN"前缀
        bg.icon = QStringLiteral("🎵");
        bg.category = QStringLiteral("source");
        bg.rect = QRectF((sceneWidth - blockWidth) / 2, 40, blockWidth, blockHeight);
        bg.color = QColor("#4CAF50");  // 绿色
        bg.enabled = true;

        // 创建图形项
        bg.gfxItem = m_scene->addRect(bg.rect);
        bg.gfxItem->setBrush(bg.color);
        bg.gfxItem->setPen(QPen(Qt::darkGray, 2));
        bg.gfxItem->setFlag(QGraphicsItem::ItemIsSelectable, false);
        
        // M1: 设置块的 ID 作为数据标记（供 drawBlockLamp 引用）
        bg.gfxItem->setData(0, QVariant(block.id));

        bg.textItem = m_scene->addText(QString("%1\n%2").arg(bg.icon).arg(bg.title));
        QFont font = bg.textItem->font();
        font.setBold(true);
        font.setPointSize(10);
        bg.textItem->setFont(font);
        bg.textItem->setDefaultTextColor(Qt::white);
        bg.textItem->setPos(bg.rect.topLeft() + QPointF(10, 20));

        m_blocks.insert(bg.id, bg);
    }

    // Layer 2: Trace, Graphic, Record（横向排列）
    {
        QString names[] = { QStringLiteral("Trace"), QStringLiteral("Graphic"), QStringLiteral("Record") };
        QColor colors[] = { QColor("#2196F3"), QColor("#FF9800"), QColor("#9C27B0") };  // 蓝橙紫
        int startX = (sceneWidth - 2 * moduleSpacing) / 2;  // 居中排列

        for (int i = 0; i < 3; ++i) {
            BlockItem item;
            item.id = QString("module_%1").arg(names[i]);
            item.title = names[i];
            item.icon = QString("📊");
            item.category = QStringLiteral("module");
            item.rect = QRectF(startX + i * moduleSpacing, 40 + layerSpacing, blockWidth, blockHeight);
            item.color = colors[i];
            item.enabled = true;

            // 创建图形项
            item.gfxItem = m_scene->addRect(item.rect);
            item.gfxItem->setBrush(item.color);
            item.gfxItem->setPen(QPen(Qt::darkGray, 2));
            item.gfxItem->setFlag(QGraphicsItem::ItemIsSelectable, false);

            item.textItem = m_scene->addText(QString("%1\n%2").arg(item.icon).arg(item.title));
            QFont font = item.textItem->font();
            font.setBold(true);
            font.setPointSize(10);
            item.textItem->setFont(font);
            item.textItem->setDefaultTextColor(Qt::white);
            item.textItem->setPos(item.rect.topLeft() + QPointF(10, 20));

            m_blocks.insert(item.id, item);
        }
    }

    // Layer 3: Real Device
    {
        BlockItem real;
        real.id = QStringLiteral("real_device");
        real.title = QStringLiteral("设备连接");
        real.icon = QStringLiteral("🔌");
        real.category = QStringLiteral("device");
        real.rect = QRectF((sceneWidth - blockWidth) / 2, 40 + layerSpacing * 2, blockWidth, blockHeight);
        real.color = QColor("#607D8B");  // 灰色
        real.enabled = true;

        // 创建图形项
        real.gfxItem = m_scene->addRect(real.rect);
        real.gfxItem->setBrush(real.color);
        real.gfxItem->setPen(QPen(Qt::darkGray, 2));
        real.gfxItem->setFlag(QGraphicsItem::ItemIsSelectable, false);

        real.textItem = m_scene->addText(QString("%1\n%2").arg(real.icon).arg(real.title));
        QFont font = real.textItem->font();
        font.setBold(true);
        font.setPointSize(10);
        real.textItem->setFont(font);
        real.textItem->setDefaultTextColor(Qt::white);
        real.textItem->setPos(real.rect.topLeft() + QPointF(10, 20));

        m_blocks.insert(real.id, real);
    }
}

void SendFlowView::drawConnections()
{
    // 清空旧连线
    for (auto *conn : m_connections) {
        m_scene->removeItem(conn);
        delete conn;
    }
    m_connections.clear();

    // SignalGenerator → Trace
    {
        BlockItem *gen = m_blocks.value(QStringLiteral("signal_generator"));
        BlockItem *trace = m_blocks.value(QStringLiteral("module_Trace"));
        if (gen && trace) {
            QPainterPath path;
            path.moveTo(gen->rect.bottomCenter());
            path.lineTo(gen->rect.bottomCenter().x(), trace->rect.topCenter().x());
            path.lineTo(trace->rect.topCenter().x(), trace->rect.topCenter().y());

            QGraphicsPathItem *item = m_scene->addPath(path);
            item->setPen(QPen(Qt::gray, 2, Qt::DashLine));
            item->setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, false);
            m_connections.append(item);
        }
    }

    // SignalGenerator → Graphic
    {
        BlockItem *gen = m_blocks.value(QStringLiteral("signal_generator"));
        BlockItem *graphic = m_blocks.value(QStringLiteral("module_Graphic"));
        if (gen && graphic) {
            QPainterPath path;
            path.moveTo(gen->rect.bottomCenter());
            path.lineTo(gen->rect.bottomCenter().x(), graphic->rect.topCenter().x());
            path.lineTo(graphic->rect.topCenter().x(), graphic->rect.topCenter().y());

            QGraphicsPathItem *item = m_scene->addPath(path);
            item->setPen(QPen(Qt::gray, 2, Qt::DashLine));
            m_connections.append(item);
        }
    }

    // SignalGenerator → Record
    {
        BlockItem *gen = m_blocks.value(QStringLiteral("signal_generator"));
        BlockItem *record = m_blocks.value(QStringLiteral("module_Record"));
        if (gen && record) {
            QPainterPath path;
            path.moveTo(gen->rect.bottomCenter());
            path.lineTo(gen->rect.bottomCenter().x(), record->rect.topCenter().x());
            path.lineTo(record->rect.topCenter().x(), record->rect.topCenter().y());

            QGraphicsPathItem *item = m_scene->addPath(path);
            item->setPen(QPen(Qt::gray, 2, Qt::DashLine));
            m_connections.append(item);
        }
    }

    // Trace → Real
    {
        BlockItem *trace = m_blocks.value(QStringLiteral("module_Trace"));
        BlockItem *real = m_blocks.value(QStringLiteral("real_device"));
        if (trace && real) {
            QPainterPath path;
            path.moveTo(trace->rect.bottomCenter());
            path.lineTo(real->rect.topCenter());

            QGraphicsPathItem *item = m_scene->addPath(path);
            item->setPen(QPen(Qt::gray, 2, Qt::DashLine));
            m_connections.append(item);
        }
    }

    // Graphic → Real
    {
        BlockItem *graphic = m_blocks.value(QStringLiteral("module_Graphic"));
        BlockItem *real = m_blocks.value(QStringLiteral("real_device"));
        if (graphic && real) {
            QPainterPath path;
            path.moveTo(graphic->rect.bottomCenter());
            path.lineTo(real->rect.topCenter());

            QGraphicsPathItem *item = m_scene->addPath(path);
            item->setPen(QPen(Qt::gray, 2, Qt::DashLine));
            m_connections.append(item);
        }
    }

    // Record → Real
    {
        BlockItem *record = m_blocks.value(QStringLiteral("module_Record"));
        BlockItem *real = m_blocks.value(QStringLiteral("real_device"));
        if (record && real) {
            QPainterPath path;
            path.moveTo(record->rect.bottomCenter());
            path.lineTo(real->rect.topCenter());

            QGraphicsPathItem *item = m_scene->addPath(path);
            item->setPen(QPen(Qt::gray, 2, Qt::DashLine));
            m_connections.append(item);
        }
    }
}

SendFlowView::BlockItem *SendFlowView::blockAt(const QPointF &scenePos)
{
    auto items = m_scene->items(scenePos);
    for (auto *item : items) {
        if (auto *rectItem = qgraphicsitem_cast<QGraphicsRectItem *>(item)) {
            for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
                if (it.value().gfxItem == rectItem)
                    return &it.value();
            }
        }
    }
    return nullptr;
}

void SendFlowView::onBlockClicked(BlockItem *block)
{
    if (!block)
        return;

    if (block->id == QStringLiteral("signal_generator")) {
        // 点击信号发生器：打开收发模块的信号发送 Tab
        emit openSendTabRequested();
    } else if (block->id == QStringLiteral("real_device")) {
        // 点击设备块：可能需要显示连接状态或跳转到设备连接页
        updateDeviceStatus();
    }
}

void SendFlowView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = m_view->mapToScene(event->pos());
        BlockItem *clickedBlock = blockAt(scenePos);
        onBlockClicked(clickedBlock);
    }

    QWidget::mousePressEvent(event);
}

void SendFlowView::updateDeviceStatus()
{
    // TODO: 更新设备连接状态的显示
    // 例如：显示当前连接的设备名称、波特率等信息
    auto *realBlock = m_blocks.value(QStringLiteral("real_device"));
    if (realBlock && m_deviceManager) {
        if (m_deviceManager->isRunning()) {
            QString deviceName = m_deviceManager->currentDeviceName();
            realBlock->textItem->setText(QString("%1\n%2 (%3)")
                .arg(realBlock->icon)
                .arg(realBlock->title)
                .arg(deviceName));
        } else {
            realBlock->textItem->setText(QString("%1\n%2 (未连接)")
                .arg(realBlock->icon)
                .arg(realBlock->title));
        }
        
        // 设备块错误状态处理
        if (!m_deviceManager->isRunning()) {
            setBlockError(QStringLiteral("real_device"), true);
        } else {
            setBlockError(QStringLiteral("real_device"), false);
        }
    }
}

// ============================================================
// M1: 流指示器系统实现
// ============================================================

void SendFlowView::onFrameReceived(const class CanFrame &frame)
{
    Q_UNUSED(frame)
    
    m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    
    // 启动闪烁定时器（如果尚未运行）
    if (!m_blinkTimer->isActive() && m_running) {
        startBlinkTimer();
    }
    
    // 检查是否仍有数据流（超过一定时间无帧则停止闪烁）
    checkFlowActivity();
}

void SendFlowView::startBlinkTimer()
{
    if (!m_blinkTimer->isActive()) {
        m_blinkTimer->start();
        emit flowActiveChanged(true);
    }
}

void SendFlowView::stopBlinkTimer()
{
    if (m_blinkTimer->isActive()) {
        m_blinkTimer->stop();
        m_blinkOn = false;
        
        // 检查是否完全停止（根据需求决定何时 emit flowActiveChanged(false)）
        // emit flowActiveChanged(false);
    }
}

void SendFlowView::checkFlowActivity()
{
    // 超过 2 秒无帧到达，认为数据流已停止
    const qint64 idleTimeoutMs = 2000;
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastFrameMs;
    
    if (elapsed > idleTimeoutMs && m_blinkTimer->isActive()) {
        stopBlinkTimer();
        updateBlockLamps();  // 恢复待命状态
    }
}

void SendFlowView::updateBlockLamps()
{
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        BlockItem &block = it.value();
        
        // 确定当前块应显示的状态灯
        BlockLamp lamp = BlockLamp::Off;
        bool blinkPhase = block.lampBlinkPhase;
        
        if (!block.enabled) {
            // 未使能：不亮灯
            lamp = BlockLamp::Off;
        } else if (m_blockErrors.contains(block.id)) {
            // 错误状态：红色闪烁
            lamp = BlockLamp::BlinkOn;
            blinkPhase = m_blinkOn;
        } else if (m_running && isFlowActiveForBlock(block.id)) {
            // 数据流活跃：绿色闪烁
            lamp = BlockLamp::BlinkOn;
            blinkPhase = m_blinkOn;
        } else if (m_running) {
            // 正在运行但暂时无数据流：常亮绿色（待命）
            lamp = BlockLamp::On;
            blinkPhase = false;
        } else {
            // 未运行：不亮灯
            lamp = BlockLamp::Off;
            blinkPhase = false;
        }
        
        // 更新块的 lamp 成员
        block.lamp = lamp;
        block.lampBlinkPhase = blinkPhase;
        
        // 绘制状态灯
        if (block.gfxItem) {
            drawBlockLamp(block.gfxItem, lamp, blinkPhase);
        }
    }
}

bool SendFlowView::isFlowActiveForBlock(const QString &blockId)
{
    // 简化版：如果有任何帧到达，就认为整个流程有数据流
    // 未来可细化到每个模块的具体统计
    qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastFrameMs;
    return elapsed < 2000;  // 2 秒内有帧到达
}

void SendFlowView::setBlockError(const QString &blockId, bool on)
{
    auto it = m_blocks.find(blockId);
    if (it == m_blocks.end())
        return;
    
    if (on) {
        m_blockErrors.insert(blockId);
    } else {
        m_blockErrors.remove(blockId);
    }
    
    updateBlockLamps();
}

void SendFlowView::updateBlockLamp(const QString &blockId, BlockLamp lamp, bool blinkPhase)
{
    auto it = m_blocks.find(blockId);
    if (it == m_blocks.end())
        return;
    
    it.value().lamp = lamp;
    it.value().lampBlinkPhase = blinkPhase;
    
    if (it.value().gfxItem) {
        drawBlockLamp(it.value().gfxItem, lamp, blinkPhase);
    }
}

void SendFlowView::drawBlockLamp(QGraphicsRectItem *gfxItem, BlockLamp lamp, bool blinkPhase)
{
    QPainter painter(gfxItem);
    QRectF rect = gfxItem->boundingRect();
    
    // 清除背景（保持原有填充）
    painter.fillRect(rect, Qt::transparent);
    
    // 绘制状态灯（位于右上角的小圆点）
    int lampRadius = 8;
    QPoint lampPos(rect.right() - lampRadius - 5,
                   rect.top() + lampRadius + 5);
    QRectF lampRect(lampPos.x() - lampRadius,
                    lampPos.y() - lampRadius,
                    lampRadius * 2, lampRadius * 2);
    
    // 根据状态选择颜色
    QColor lampColor;
    
    // 检查是否是错误状态（通过块 ID）
    bool isErrorResponse = m_blockErrors.contains(gfxItem->data(0).toString());
    
    if (blinkPhase && lamp == BlockLamp::BlinkOn) {
        if (isErrorResponse) {  // 错误状态
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
        // 发光效果
        gradient.setColorAt(0.0, lampColor.lighter(150));
        gradient.setColorAt(0.6, lampColor);
        gradient.setColorAt(1.0, lampColor.darker(150));
    } else {
        // 普通颜色
        gradient.setColorAt(0.0, lampColor);
        gradient.setColorAt(1.0, lampColor.darker(150));
    }
    
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(lampRect);
}

void SendFlowView::drawLampIndicator(QPainter *painter, QRectF rect, BlockLamp lamp, bool blinkPhase)
{
    // 备用绘制函数（供 future extension 使用）
    Q_UNUSED(painter)
    Q_UNUSED(rect)
    Q_UNUSED(lamp)
    Q_UNUSED(blinkPhase)
}
