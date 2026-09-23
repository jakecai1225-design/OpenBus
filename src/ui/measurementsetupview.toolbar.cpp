#include "measurementsetupview.h"
#include <QFileDialog>
#include <QMessageBox>

// ============================================================
//  State control and toolbar handlers — 状态控制和工具栏处理
// ============================================================

void MeasurementSetupView::setSource(Source src)
{
    m_source = src;
    // Update enabled state of data source blocks (only control file block, don't interfere with signal_generator and source_real)
    if (m_blocks.contains("source_file"))
        m_blocks["source_file"].enabled = (src == Source::File);
    
    // When switching to Hardware mode, ensure source_real is in enabled state
    if (src == Source::Hardware && m_blocks.contains("source_real")) {
        m_blocks["source_real"].enabled = true;  // Hardware mode auto-enables Real
    }
    
    // Update connections: replace old data source connection with new data source
    // (deduplication—same data source back-and-forth switching doesn't accumulate overlapping connections)
    QString oldId = (src == Source::Hardware) ? "source_file" : "source_real";
    QString newId = (src == Source::Hardware) ? "source_real" : "source_file";
    for (auto &conn : m_connections) {
        if (conn.fromId == oldId)
            conn.fromId = newId;
    }
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        for (int j = 0; j < i; ++j) {
            if (m_connections[j].fromId == m_connections[i].fromId
                && m_connections[j].toId == m_connections[i].toId) {
                m_connections.removeAt(i);
                break;
            }
        }
    }
    rebuildScene();
}

void MeasurementSetupView::setFilePath(const QString &path)
{
    m_filePath = path;
    rebuildScene();
}

void MeasurementSetupView::onFrame(const CanFrame &)
{
    // Data flow indication: record latest frame arrival time (data stream active detection driver;
    // frame statistics displayed uniformly by MainWindow status bar)
    m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_blinkTimer->isActive()) {
        m_blinkOn = true;  // First frame start blinking from lit phase
        m_blinkTimer->start();
        updateBlockLamps();
    }
}

void MeasurementSetupView::setRunning(bool running)
{
    // Pure status reset (offline playback end / cancel): sync button states only.
    m_running = running;
    m_startAct->setEnabled(!running);
    m_stopAct->setEnabled(running);
    if (m_startBtn)
        m_startBtn->setEnabled(!running);
    if (m_stopBtn)
        m_stopBtn->setEnabled(running);
    // Replay stays available to restart from the beginning at any time.
    if (m_replayAct)
        m_replayAct->setEnabled(true);
    if (m_replayBtn)
        m_replayBtn->setEnabled(true);
    m_lastFrameMs = 0;
    updateBlockLamps();
}

void MeasurementSetupView::onStartClicked()
{
    setRunning(true);
    emit measurementToggled(true);
}

void MeasurementSetupView::onReplayClicked()
{
    setRunning(true);
    emit measurementReplayRequested();
}

void MeasurementSetupView::onStopClicked()
{
    setRunning(false);
    emit measurementToggled(false);
}

void MeasurementSetupView::onBrowseClicked()
{
    emit fileBrowseRequested();
}
