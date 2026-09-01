#include "measurementsetupview.h"
#include "ui/measurementsetupview.gfx.h"
#include "ui/thememanager.h"
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QPen>
#include <QBrush>
#include <cmath>
#include <QScrollBar>

// ============================================================
//  Scene rebuilding and connection drawing — 场景重建
// ============================================================

void MeasurementSetupView::rebuildScene()
{
    m_scene->clear();
    
    // Update module block heights based on instance count
    const qreal baseH = 60;
    const qreal rowH = 22;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        if (b.category == "module" || b.category == "filter") {
            // Trace/Graphic blocks occupy their own row without sub-instances
            // Other modules and Filter block adjust height based on content
            if (b.moduleName != "trace" && b.moduleName != "graphic") {
                qreal h = baseH;
                if (!b.instances.isEmpty())
                    h = baseH + b.instances.size() * rowH;
                b.rect.setHeight(h);
                
                for (int i = 0; i < b.instances.size(); ++i) {
                    b.instances[i].subRect = QRectF(
                        b.rect.left() + 2, b.rect.top() + baseH + i * rowH,
                        b.rect.width() - 4, rowH);
                }
            }
        }
    }
    
    // Draw connections first (they appear behind blocks)
    updateConnections();
    
    // Draw blocks
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        bool active = b.enabled;
        
        // Data source blocks: use unified green for active, gray for inactive
        if (b.category == "source") {
            // Unified active color (green), inactive color (gray)
            b.color = active ? QColor(0x4C, 0xAF, 0x50) : QColor(0x80, 0x80, 0x80);
            
            if (b.id == "source_real") {
                b.title = QStringLiteral("Real 实时");
                // Real's state controlled by activeSourceId()
                active = (b.id == activeSourceId());
            } else if (b.id == "source_file") {
                b.title = QStringLiteral("离线分析");
                // Offline analysis state controlled by activeSourceId()
                active = (b.id == activeSourceId());
            } else if (b.id == "signal_generator") {
                b.title = QStringLiteral("信号发生器");
                // signal_generator directly uses enabled state
                active = b.enabled;
            }
        }
        
        auto *item = new SetupBlockGfx(b.rect, b.icon, b.title, b.color, active,
                                        b.category == "source");
        // Pass instance list to rendering item (Filter block instances = filter rules)
        if ((b.category == "module" || b.category == "filter")
            && b.moduleName != "trace" && b.moduleName != "graphic") {
            QStringList instTitles;
            for (const auto &inst : b.instances)
                instTitles << inst.title;
            item->setInstances(instTitles);
        }
        if (b.category == "filter")
            item->setRuleMode(true);
        m_scene->addItem(item);
        b.gfxItem = item;
    }
    
    // Draw data source switch
    if (!m_switchRect.isNull()) {
        auto *switchItem = new SourceSwitchGfx(m_switchRect, m_source == Source::Hardware);
        m_scene->addItem(switchItem);
    }
    
    // Update scene rect to accommodate all blocks + switch
    QRectF sceneRect;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it)
        sceneRect = sceneRect.united(it.value().rect);
    sceneRect = sceneRect.united(m_switchRect);
    if (!sceneRect.isNull())
        m_scene->setSceneRect(sceneRect.adjusted(-20, -20, 40, 20));
    
    // Re-project lamp states after gfx rebuild (block enable/data flow/error states exist at view layer)
    updateBlockLamps();
}
