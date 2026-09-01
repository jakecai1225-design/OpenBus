#include "measurementsetupview.h"
#include <QMessageBox>

// ============================================================
//  Module instance management — 模块实例动态增删
// ============================================================

void MeasurementSetupView::addModuleInstance(const QString &moduleName, const QString &instanceId, const QString &title)
{
    if (m_blocks.contains(instanceId))
        return;  // Already exists
    
    if (moduleName == "trace" || moduleName == "graphic") {
        // Trace/Graphic: each instance gets its own block, stacked horizontally
        BlockItem b;
        b.id = instanceId;
        b.title = title;
        if (moduleName == "trace") {
            b.icon = "";
            b.color = QColor(0x21, 0x96, 0xF3);
        } else {
            b.icon = "";
            b.color = QColor(0xF4, 0x43, 0x36);
        }
        b.category = "module";
        b.moduleName = moduleName;
        // Temporary position, relayoutModuleBlocks will recompute
        b.rect = QRectF(50, 300, 140, 60);
        m_blocks[instanceId] = b;
        
        // Add database → module block connection
        Connection c;
        c.fromId = "database";
        c.toId = instanceId;
        c.pathItem = nullptr;
        m_connections.append(c);
        
        relayoutModuleBlocks();
    } else {
        // Other modules: add instance to existing block
        auto it = m_blocks.find(moduleName);
        if (it == m_blocks.end()) return;
        auto &b = it.value();
        for (const auto &inst : b.instances) {
            if (inst.id == instanceId) return;  // Already exists
        }
        InstanceItem item;
        item.id = instanceId;
        item.title = title;
        b.instances.append(item);
        rebuildScene();
    }
}

void MeasurementSetupView::removeModuleInstance(const QString &moduleName, const QString &instanceId)
{
    if (moduleName == "trace" || moduleName == "graphic") {
        // Trace/Graphic: delete entire block
        if (!m_blocks.contains(instanceId)) return;
        m_blocks.remove(instanceId);
        for (int i = m_connections.size() - 1; i >= 0; --i) {
            if (m_connections[i].fromId == instanceId || m_connections[i].toId == instanceId)
                m_connections.removeAt(i);
        }
        relayoutModuleBlocks();
    } else {
        // Other modules: remove instance from block
        auto it = m_blocks.find(moduleName);
        if (it == m_blocks.end()) return;
        auto &b = it.value();
        for (int i = b.instances.size() - 1; i >= 0; --i) {
            if (b.instances[i].id == instanceId) {
                b.instances.removeAt(i);
                rebuildScene();
                return;
            }
        }
    }
}

void MeasurementSetupView::clearTraceGraphicInstances()
{
    // Collect all Trace/Graphic instance block IDs (blocks starting with "trace" or "graphic")
    QStringList toRemove;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        const auto &b = it.value();
        if (b.category == "module" &&
            (b.moduleName == "trace" || b.moduleName == "graphic"))
            toRemove << it.key();
    }
    for (const auto &id : toRemove) {
        m_blocks.remove(id);
        for (int i = m_connections.size() - 1; i >= 0; --i) {
            if (m_connections[i].fromId == id || m_connections[i].toId == id)
                m_connections.removeAt(i);
        }
    }
    if (!toRemove.isEmpty())
        relayoutModuleBlocks();
}

void MeasurementSetupView::removeModuleBlock(const QString &blockId)
{
    m_blocks.remove(blockId);
    
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        if (m_connections[i].fromId == blockId || m_connections[i].toId == blockId)
            m_connections.removeAt(i);
    }
    
    relayoutModuleBlocks();
}

void MeasurementSetupView::relayoutModuleBlocks()
{
    // Collect module blocks by type, sort by ID
    QStringList traceIds, graphicIds, otherIds;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        const auto &b = it.value();
        if (b.category != "module") continue;
        if (b.moduleName == "trace")
            traceIds << it.key();
        else if (b.moduleName == "graphic")
            graphicIds << it.key();
        else
            otherIds << it.key();
    }
    traceIds.sort();
    graphicIds.sort();

    // Module blocks stack vertically to the right of DBC block
    const qreal moduleH = 60;
    const qreal modW = 140;
    const qreal modGap = 16;       // Vertical spacing
    qreal moduleX = 0;
    qreal moduleY = 30;
    {
        auto dbIt = m_blocks.find("database");
        if (dbIt != m_blocks.end())
            moduleX = dbIt->rect.right() + 60;  // To the right of DBC + gapX
        else
            moduleX = 750;  // fallback
    }

    // Combine all module IDs, sorted in order: Trace → Graphic → Other
    QStringList allIds;
    allIds << traceIds << graphicIds << otherIds;

    qreal y = moduleY;
    for (int i = 0; i < allIds.size(); ++i) {
        auto it = m_blocks.find(allIds[i]);
        if (it != m_blocks.end()) {
            it.value().rect = QRectF(moduleX, y, modW, moduleH);
            y += moduleH + modGap;
        }
    }

    rebuildScene();
}
