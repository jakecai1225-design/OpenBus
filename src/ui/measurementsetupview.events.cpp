#include "measurementsetupview.h"
#include <QDateTime>

// ============================================================
 //  Canvas event handling
 // ============================================================

void MeasurementSetupView::onSceneClicked(const QPointF &scenePos)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    // Check if clicked on switch button
    if (m_switchRect.contains(scenePos)) {
        Source newSrc = (m_source == Source::Hardware) ? Source::File : Source::Hardware;
        setSource(newSrc);
        emit sourceChanged(static_cast<int>(newSrc));
        m_lastClickTime = now;  // Record click time for double-click detection
        return;
    }
    
    QString moduleId, instanceId;
    if (instanceAt(scenePos, moduleId, instanceId)) {
        emit moduleOpened(moduleId, instanceId);
        m_lastClickTime = now;  // Record click time for double-click detection
        return;
    }
    
    auto *b = blockAt(scenePos);
    if (!b) return;
    
    // Unified interaction rules:
    // - data source blocks: Real/File mutual exclusion with auto-switch; signal_generator independent toggle
    // - Filter/CAN parser/Watcher/Record: enabled toggling on single-click
    // - Trace/Graphic independent blocks: enabled toggling
    if (b->category == "source") {
        if (b->id == "signal_generator") {
            // Signal generator: independent toggle
            setBlockEnabled(b->id, !b->enabled);
            m_lastClickTime = now;  // Record click time for double-click detection
        } else {
            // Real/File mutual exclusion with auto-activation of current block, deactivation of other
            Source newSrc = (b->id == "source_real") ? Source::Hardware : Source::File;
            setSource(newSrc);
            emit sourceChanged(static_cast<int>(newSrc));
            m_lastClickTime = now;  // Record click time for double-click detection
        }
    } else if (b->category == "filter" || b->category == "database" ||
               b->id == "watcher" || b->id == "record") {
        // Filter/CAN parser/Watcher/Record: enable toggle with time recording for double-click detection
        setBlockEnabled(b->id, !b->enabled);
        m_lastClickTime = now;  // Record click time for double-click detection
    } else if (b->category == "module" && (b->moduleName == "trace" || b->moduleName == "graphic")) {
        // Trace/Graphic independent blocks: enable toggle
        setBlockEnabled(b->id, !b->enabled);
        m_lastClickTime = now;  // Record click time for double-click detection
    }
}

void MeasurementSetupView::onSceneDoubleClicked(const QPointF &scenePos)
{
    auto *b = blockAt(scenePos);
    if (!b) return;

    // Double-click detection: two clicks within a short time window -> open config page/tab
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool isFastDoubleClick = (now - m_lastClickTime < DOUBLE_CLICK_INTERVAL);

    // Data source blocks and module blocks handled separately
    if (b->category == "source") {
        if (b->id == "source_real") {
            // Real: double-click opens connection configuration (only when enabled)
            if (isFastDoubleClick && b->enabled) {
                emit realBlockClicked();
            }
        } else if (b->id == "source_file") {
            // Offline analysis: double-click opens file selection (only when enabled)
            if (isFastDoubleClick && b->enabled) {
                emit fileBlockClicked();
            }
        } else if (b->id == "signal_generator") {
            // Signal generator: double-click opens send configuration
            // Regardless of enable state, allow opening configuration
            if (isFastDoubleClick) {
                emit sendPageOpened();
            }
        }
    } else if (b->category == "filter" || b->category == "database") {
        // Filter / CAN parser: same as other config blocks —
        // select+enable, then open config dialog (single-click may have
        // toggled enable; force ON so config opens on an active block).
        setBlockEnabled(b->id, true);
        openBlockConfig(b->id);
    } else if (b->id == "watcher" || b->id == "record") {
        // Same pattern: enable then open page via shell
        setBlockEnabled(b->id, true);
        openBlockConfig(b->id);
    } else if (b->category == "module" && (b->moduleName == "trace" || b->moduleName == "graphic")) {
        // Trace / Graphic independent blocks (e.g., trace1, graphic1)
        // Double-click jumps to the tab for this block
        if (isFastDoubleClick) {
            emit moduleOpened(b->moduleName, b->id);
        }
        return;
    }
}
