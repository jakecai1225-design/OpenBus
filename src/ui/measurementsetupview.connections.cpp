#include "measurementsetupview.h"
#include "ui/measurementsetupview.gfx.h"
#include <QGraphicsPathItem>
#include <QPainterPath>
#include <QTimer>
#include <cmath>

// ============================================================
//  Connection drawing and block visual updates — 连线绘制与块状态更新
// ============================================================

void MeasurementSetupView::updateConnections()
{
    for (auto &conn : m_connections) {
        auto fromIt = m_blocks.find(conn.fromId);
        auto toIt = m_blocks.find(conn.toId);
        if (fromIt == m_blocks.end() || toIt == m_blocks.end())
            continue;
        
        QRectF from = fromIt->rect;
        QRectF to = toIt->rect;
        
        // Horizontal connection: from right side of 'from' to left side of 'to'
        QPointF start = QPointF(from.right(), from.center().y());
        QPointF end = QPointF(to.left(), to.center().y());
        
        // If source block is to the right of destination, reverse direction
        if (from.left() > to.right()) {
            start = QPointF(from.left(), from.center().y());
            end = QPointF(to.right(), to.center().y());
        }
        
        // Check both ends are enabled
        bool fromActive = fromIt->enabled;
        bool toActive = toIt->enabled;
        
        // Source-type blocks have special activation rules:
        // - signal_generator: uses enabled directly
        // - source_real/source_file: controlled by activeSourceId()
        if (fromIt->category == "source") {
            if (fromIt->id == "signal_generator") {
                fromActive = fromIt->enabled;  // Signal generator independent control
            } else {
                fromActive = (fromIt->id == activeSourceId());  // Real/File controlled by m_source
            }
        }
        
        // Draw path
        QPainterPath path;
        path.moveTo(start);
        
        qreal midX = (start.x() + end.x()) / 2;
        if (qAbs(start.y() - end.y()) < 2) {
            // On same horizontal line — straight line
            path.lineTo(end);
        } else {
            // Z-shaped path: right → vertical → right
            path.lineTo(QPointF(midX, start.y()));
            path.lineTo(QPointF(midX, end.y()));
            path.lineTo(end);
        }
        
        auto *pathItem = new QGraphicsPathItem();
        pathItem->setPath(path);
        QColor lineColor = (fromActive && toActive) ? QColor(0xa0, 0xa0, 0xa0) : QColor(0xd0, 0xd0, 0xd0);
        Qt::PenStyle style = (fromActive && toActive) ? Qt::SolidLine : Qt::DashLine;
        pathItem->setPen(QPen(lineColor, 1.8, style, Qt::RoundCap, Qt::RoundJoin));
        
        m_scene->addItem(pathItem);
        conn.pathItem = pathItem;
        
        // Arrow (pointing right)
        qreal arrowSize = 6;
        QPointF arrowP1 = QPointF(end.x() - arrowSize, end.y() - arrowSize);
        QPointF arrowP2 = QPointF(end.x() - arrowSize, end.y() + arrowSize);
        
        QPainterPath arrowPath;
        arrowPath.moveTo(end);
        arrowPath.lineTo(arrowP1);
        arrowPath.lineTo(arrowP2);
        arrowPath.closeSubpath();
        
        auto *arrowItem = new QGraphicsPathItem();
        arrowItem->setPath(arrowPath);
        arrowItem->setBrush(lineColor);
        arrowItem->setPen(QPen(lineColor, 1));
        m_scene->addItem(arrowItem);
    }
}

void MeasurementSetupView::updateBlockVisual(const QString &id)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;
    
    auto &b = it.value();
    if (b.gfxItem) {
        auto *gfx = dynamic_cast<SetupBlockGfx*>(b.gfxItem);
        if (gfx) {
            bool active = b.enabled;
            // Data source blocks light up based on "current active data source"
            // (inactive side appears grayed out)
            if (b.category == "source") {
                if (b.id == "source_real") {
                    gfx->setTitle(QStringLiteral("Real 实时"));
                } else {
                    gfx->setTitle(QStringLiteral("离线分析"));
                }
                active = (b.id == activeSourceId());
            }
            gfx->setActive(active);
        }
    }
    // Enable state changes immediately refresh lamp states (no need to wait for blink tick)
    updateBlockLamps();
}

void MeasurementSetupView::updateBlockLamps()
{
    // Data flow active detection: measurement running AND new frame within last 1.5s
    // (no new frames beyond 1.5s → flow inactive, green flash returns to solid idle)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool flowActive = m_running && m_lastFrameMs > 0
                            && (now - m_lastFrameMs) < 1500;
    
    // Per-block lamp state: error takes priority (including data source blocks)
    // → disabled means no light → ready/flow-active
    auto lampFor = [this, flowActive](const QString &id, const BlockItem &b) {
        if (m_blockErrors.contains(id))
            return BlockLamp::Error;
        const bool isSource = b.category == QStringLiteral("source");
        const bool active = isSource ? (b.id == activeSourceId()) : b.enabled;
        if (!active)
            return BlockLamp::None;  // Disabled: no light (block already grayed out)
        
        // Data source blocks show no normal-state light (only red blink on error)
        if (isSource)
            return BlockLamp::None;
        
        return flowActive ? BlockLamp::Flow : BlockLamp::Idle;
    };
    
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto *gfx = dynamic_cast<SetupBlockGfx *>(it.value().gfxItem);
        if (gfx)
            gfx->setLamp(lampFor(it.key(), it.value()), m_blinkOn);
    }
    
    // No data flow and no errors: stop blink timer (power saving; static lamp state)
    if (!flowActive && m_blockErrors.isEmpty() && m_blinkTimer)
        m_blinkTimer->stop();
}
