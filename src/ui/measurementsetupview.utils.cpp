#include "measurementsetupview.h"
#include <QDateTime>

// ============================================================
//  Utility functions — 工具函数与辅助查询
// ============================================================

QString MeasurementSetupView::activeSourceId() const
{
    // Consider signal generator as optional data source
    if (m_source == Source::Hardware) 
        return "source_real";
    else if (m_source == Source::File)
        return "source_file";
    return QString();  // No active data source
}

void MeasurementSetupView::setBlockError(const QString &blockId, bool on)
{
    if (!m_blocks.contains(blockId))
        return;  // Unknown block—silently ignore
    
    const bool changed = on ? !m_blockErrors.contains(blockId)
                            : m_blockErrors.remove(blockId) > 0;
    if (!changed)
        return;
    
    // Start blinking timer if error appears
    if (on && m_blinkTimer && !m_blinkTimer->isActive()) {
        m_blinkOn = true;  // Start with lit phase for immediate red blink
        m_blinkTimer->start();
    }
    updateBlockLamps();
}

MeasurementSetupView::BlockItem *MeasurementSetupView::blockAt(const QPointF &scenePos)
{
    // Hit-test: find block whose rect contains the scene position
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        if (it.value().rect.contains(scenePos))
            return &it.value();
    }
    return nullptr;
}

bool MeasurementSetupView::instanceAt(const QPointF &scenePos, QString &moduleId, QString &instanceId)
{
    // Hit-test instance sub-rects of module blocks
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        if (b.category != "module") continue;
        for (const auto &inst : b.instances) {
            if (inst.subRect.contains(scenePos)) {
                moduleId = b.id;
                instanceId = inst.id;
                return true;
            }
        }
    }
    return false;
}

void MeasurementSetupView::toggleBlock(const QString &id)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;
    setBlockEnabled(id, !it->enabled);
}

void MeasurementSetupView::setBlockEnabled(const QString &id, bool enabled)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;
    auto &b = it.value();
    
    // Signal generator allows toggle via double-click; real/file cannot be directly disabled
    if (b.category == "source") {
        if (b.id != "signal_generator")
            return;
    }
    if (b.enabled == enabled) return;
    
    b.enabled = enabled;
    rebuildScene();
    emit moduleToggled(b.id, b.moduleName, b.enabled);
}

void MeasurementSetupView::openBlockConfig(const QString &blockId)
{
    // Unified entry point for already-enabled block click/double-click/right-click [Configure]
    auto it = m_blocks.find(blockId);
    if (it == m_blocks.end()) return;
    const auto &b = it.value();
    
    if (b.category == "filter") {
        showFilterConfigDialog();
    } else if (b.category == "database") {
        showDbcSelectDialog();
    } else if (b.category == "module") {
        if (b.moduleName == "trace") {
            emit moduleOpened("trace", b.id);
        } else if (b.moduleName == "graphic") {
            emit moduleOpened("graphic", b.id);
        } else {
            emit moduleOpened(b.id, "");
        }
    }
}

bool MeasurementSetupView::isBlockEnabled(const QString &blockId) const
{
    auto it = m_blocks.find(blockId);
    if (it == m_blocks.end())
        return true;  // Non-existent blocks default to enabled
    return it->enabled;
}
