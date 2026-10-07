#include "mainwindow.h"
#include <QTimer>
#include "core/canframe.h"
#include "core/recorder.h"
#include "core/player.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"
#include "models/cantracemodel.h"
#include "models/cantraceproxymodel.h"
#include "ui/activitybar.h"
#include "ui/panels/sidebarpanels.h"
#include "ui/thememanager.h"
#include "ui/bottompanel.h"
#include "ui/rightpanel.h"
#include "ui/spliteditorarea.h"
// ui/measurementsetupview.h / ui/deviceconnectiontab.h 已移除 —
// Flow/设备连接页经 ModuleRegistry "flow" 模块创建（拆分方案 B4）
#include "core/driver/driverregistry.h"
#include "core/module/moduleregistry.h"
#include "core/module/imodule.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "core/marketmodel.h"   // MarketItem（ExtensionsPanel 信号类型，经 QVariant 传给市场模块；B5-5 迁 data 层）
// ui/udsview.h, ui/canopenview.h 已移除 — UDS/CANopen 由插件 uds-diagnostic/canopen-explorer 提供
// ui/markettab.h 已移除 — 插件市场页经 ModuleRegistry "market" 模块创建（拆分方案 B0）
// ui/signalsendtab.h / playbacktab.h / offlineanalysistab.h / recordtab.h 已移除 —
// 收发四页经 ModuleRegistry "transceive" 模块创建（拆分方案 B2）
// ui/dbcdetailtab.h / ui/tools/dbcsignallistview.h 已移除 —
// DBC 页经 ModuleRegistry "dbc" 模块创建（拆分方案 B3）
// ui/traceview.h / ui/graphicview.h / ui/datawindow.h / ui/filterbar.h /
// ui/colorruleeditor.h 已移除 — Trace/Graphic/DataWindow/着色规则经
// ModuleRegistry "trace"/"graphic" 模块创建与操控（拆分方案 B5）
#include "ui/tools/iographview.h"
#include "core/busstatistics.h"
// core/filterpresetmanager.h 已移除 — 过滤预设随 Trace 页迁入 TraceModule（B5）
#include "core/bookmarkmanager.h"
// core/triggerrecorder.h 已移除 — 触发录制随录制页迁入 transceive 模块（拆分方案 B2）
#include "utils/canutils.h"
#include "core/appconfig.h"
#include "core/projectmanager.h"
#include "utils/svg_icon.h"
#include "ui/settingspage.h"
#include "ui/shortcutspage.h"
#include "core/file_import/file_importer.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"
#include "models/viewportproxy.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QStatusBar>
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QMouseEvent>
#include <QWindow>
#include <QDesktopServices>
#include <QUrl>
#include <QLineEdit>
#include <QSlider>
#include <QComboBox>
#include <QProgressDialog>
#include <QRegularExpression>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ============================================================
//  MainWindow 用户动作（B6 拆分自 mainwindow.cpp）
//  录制/回放/清屏/打开/导入 + 右侧面板快捷按钮 + 底部终端命令 +
//  统计与动作状态刷新
// ============================================================

// ============================================================
//  录制 / 回放
// ============================================================

void MainWindow::onRecord()
{
    if (m_recording) {
        m_recorder->stop();
    } else {
        QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".asc";
        QString path = QFileDialog::getSaveFileName(
            this, tr("Recording File"), defaultName, CanFileIO::writableFileFilters());
        if (path.isEmpty()) {
            m_recordAction->setChecked(false);
            return;
        }
        if (!CanFileIOFactory::canWrite(QFileInfo(path).suffix())) {
            QMessageBox::warning(this, tr("Record"),
                tr("Unsupported recording format: .%1\nPlease use ASC or CSV.")
                    .arg(QFileInfo(path).suffix()));
            m_recordAction->setChecked(false);
            return;
        }
        if (!m_recorder->start(path)) {
            QMessageBox::warning(this, tr("Record"),
                tr("Cannot create file: %1\nCheck that the path is valid and disk space is available.")
                    .arg(path));
            m_recordAction->setChecked(false);
            m_bottomPanel->addProblem(1, QStringLiteral("Recorder"),
                tr("Cannot create recording file: %1").arg(path));
            return;
        }
    }
}

void MainWindow::onPlay()
{
    if (!m_player->isLoaded()) {
        // 优先从离线分析标签页加载（文件列表经 transceive 模块查询，B2）
        const QStringList paths = transceiveQuery(
            QStringLiteral("offlineFiles")).toStringList();
        if (!paths.isEmpty()) {
            QVector<CanFrame> allFrames;
            for (const auto &path : paths) {
                auto reader = CanFileIOFactory::createReader(path);
                if (!reader || !reader->open(path)) continue;
                QVector<CanFrame> frames;
                reader->readAll(frames);
                reader->close();
                allFrames += frames;
            }
            if (!allFrames.isEmpty()) {
                std::sort(allFrames.begin(), allFrames.end(),
                          [](const CanFrame &a, const CanFrame &b) {
                              return a.timestamp < b.timestamp;
                          });
                m_player->loadFrames(allFrames);
            }
        }
        if (!m_player->isLoaded())
            onOpenFile();
        if (!m_player->isLoaded()) return;
    }
    m_player->play();
}

void MainWindow::onPause() { m_player->pause(); }
void MainWindow::onStop()  { m_player->stop(); }

void MainWindow::onClear()
{
    // 清除所有 Trace 模型和 Graphic 视图（经模块，拆分方案 B5）
    graphicInvoke(QStringLiteral("clearDataAll"));
    traceInvoke(QStringLiteral("clearAll"));
    updateStatistics();
}

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, tr("Open File"), {},
        CanFileIO::allFileFilters(false) + tr(";;DBC Files (*.dbc);;All Files (*.*)"));
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (fi.suffix().toLower() == "dbc") {
        if (!m_dbcManager->loadDbc(path))
            m_bottomPanel->addProblem(1, QStringLiteral("DBC"),
                tr("Load failed: %1").arg(path));
    } else {
        if (!m_player->load(path)) {
            QMessageBox::warning(this, tr("Open"), tr("Cannot load: %1").arg(path));
            m_bottomPanel->addProblem(1, QStringLiteral("Player"),
                tr("Cannot load: %1").arg(path));
            return;
        }
        // 清除所有 Trace 和 Graphic 视图（经模块，拆分方案 B5）
        graphicInvoke(QStringLiteral("clearDataAll"));
        traceInvoke(QStringLiteral("clearTraceAll"));
        m_bottomPanel->appendOutput(tr("Loaded: %1 (%2 frames, %3s)")
            .arg(fi.fileName()).arg(m_player->totalFrames())
            .arg(m_player->totalTime(), 0, 'f', 2));
        transceiveInvoke(QStringLiteral("setFileInfo"),
            QVariantList{ fi.fileName(), m_player->totalFrames(), m_player->totalTime() });
    }
    updateActions();
}

void MainWindow::onImportLog()
{
    const auto filters = FileImportFactory::fileFilters();
    QString path = QFileDialog::getOpenFileName(
        this, tr("Import Log File"), {},
        filters.join(QStringLiteral(";;")));
    if (path.isEmpty()) return;

    auto importer = FileImportFactory::create(path);
    if (!importer) {
        QMessageBox::warning(this, tr("Import"), tr("Unsupported file format"));
        return;
    }

    // Progress dialog (show after 500ms to avoid flicker on small files)
    QProgressDialog progress(tr("Importing..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    auto frames = importer->importFile(path, [&progress](double pct) {
        progress.setValue(static_cast<int>(pct * 100));
        QApplication::processEvents();
    });

    if (progress.wasCanceled()) return;

    if (frames.isEmpty()) {
        QMessageBox::warning(this, tr("Import"),
            tr("File is empty or could not be parsed"));
        return;
    }

    // 找到活跃的 TraceTab（当前页或第一个找到的；经 trace 模块，拆分方案 B5）
    QWidget *traceTab = m_editorArea->currentWidget();
    if (!traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(traceTab)).toBool()) {
        traceTab = nullptr;
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                QWidget *w = tw->widget(i);
                if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(w)).toBool()) {
                    traceTab = w;
                    break;
                }
            }
            if (traceTab) break;
        }
    }

    if (traceTab) {
        QVariantList framesVar;
        framesVar.reserve(frames.size());
        for (const auto &f : frames)
            framesVar.append(QVariant::fromValue(f));
        traceInvoke(QStringLiteral("appendFrames"),
                    QVariantList{ QVariant::fromValue(traceTab), framesVar });
    }

    const QFileInfo fi(path);
    const int count = traceTab
        ? traceQuery(QStringLiteral("frameCount"), QVariant::fromValue(traceTab)).toInt()
        : frames.size();
    m_bottomPanel->appendOutput(tr("Import complete: %1 (%2 frames, format: %3)")
        .arg(fi.fileName()).arg(frames.size()).arg(importer->formatName()));
    m_frameCountLabel->setText(tr("%1 frames").arg(count));
    m_rowCountLabel->setText(tr("%1 rows").arg(count));
    m_statusLabel->setText(tr("Imported %1").arg(fi.fileName()));
}

void MainWindow::onAutoScrollToggled(bool on)
{
    m_autoScroll = on;
    // 同步所有 Trace 页滚动状态（经 trace 模块，拆分方案 B5）
    traceInvoke(QStringLiteral("setAutoScroll"), on);
}


// ============================================================
//  Command line / terminal
// ============================================================

void MainWindow::onCommandEntered(const QString &cmd)
{
    processCommand(cmd);
}

void MainWindow::processCommand(const QString &cmd)
{
    auto *out = m_bottomPanel;
    if (cmd == "help" || cmd == "?") {
        out->appendTerminal("Available commands:");
        out->appendTerminal("  help              - show this help");
        out->appendTerminal("  clear             - clear Trace");
        out->appendTerminal("  sim on / sim off  - start/stop simulator");
        out->appendTerminal("  dev on / dev off  - start/stop hardware device");
        out->appendTerminal("  record <file>     - start recording");
        out->appendTerminal("  stop              - stop record/playback");
        out->appendTerminal("  play              - start playback");
        out->appendTerminal("  filter <expr>     - set Trace filter");
        out->appendTerminal("  stats             - show frame stats");
        out->appendTerminal("  load <file>       - load DBC or capture file");
        out->appendTerminal("  bash / shell      - start MSYS2 bash in this pane");
        out->appendTerminal("  powershell        - start PowerShell in this pane");
        out->appendTerminal("  clear-term / cls  - clear terminal screen");
    } else if (cmd == "clear") {
        onClear();
        out->appendTerminal("Trace cleared");
    } else if (cmd == "sim on") {
        m_simulator->start();
        out->appendTerminal("Simulator started");
    } else if (cmd == "sim off") {
        m_simulator->stop();
        out->appendTerminal("Simulator stopped");
    } else if (cmd == "dev on") {
        m_deviceManager->start();
        out->appendTerminal("Hardware device started");
    } else if (cmd == "dev off") {
        m_deviceManager->stop();
        out->appendTerminal("Hardware device stopped");
    } else if (cmd.startsWith("record ")) {
        QString path = cmd.mid(7).trimmed();
        if (m_recorder->start(path))
            out->appendTerminal("Recording started: " + path);
        else
            out->appendTerminal("Recording failed: " + path);
    } else if (cmd == "stop") {
        if (m_recording) m_recorder->stop();
        m_player->stop();
        out->appendTerminal("Stopped");
    } else if (cmd == "play") {
        onPlay();
        out->appendTerminal("Playback started");
    } else if (cmd.startsWith("filter ")) {
        QString expr = cmd.mid(7).trimmed();
        // Terminal filter: apply to active Trace page via module API
        QWidget *active = m_editorArea->currentWidget();
        if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(active)).toBool())
            traceInvoke(QStringLiteral("setFilterExpression"),
                        QVariantList{ QVariant::fromValue(active), expr });
        out->appendTerminal("Filter: " + expr);
    } else if (cmd == "stats") {
        updateStatistics();
        QWidget *active = m_editorArea->currentWidget();
        int total = traceQuery(QStringLiteral("frameCount"),
                               QVariant::fromValue(active)).toInt();
        out->appendTerminal(QString("Total frames: %1").arg(total));
    } else if (cmd.startsWith("load ")) {
        QString path = cmd.mid(5).trimmed();
        QFileInfo fi(path);
        if (fi.suffix().toLower() == "dbc") {
            m_dbcManager->loadDbc(path);
        } else if (CanFileIOFactory::canRead(fi.suffix())) {
            if (m_player->load(path)) {
                // Clear all Trace pages via module broadcast
                traceInvoke(QStringLiteral("clearTraceAll"));
                out->appendTerminal("Loaded capture: " + fi.fileName());
                updateActions();
            } else {
                out->appendTerminal("Load failed: " + fi.fileName());
            }
        } else {
            out->appendTerminal("Unsupported format: ." + fi.suffix());
        }
    } else {
        out->appendTerminal("Unknown command: " + cmd + " (type help)");
    }
}

// ============================================================
//  统计 & 状态
// ============================================================

void MainWindow::updateStatistics()
{
    if (m_measurementRunning) {
        if (m_player->isLoaded()) {
            int total = m_player->totalFrames();
            m_frameCountLabel->setText(
                tr("%1 / %2 frames").arg(m_receivedFrameCount).arg(total));
        } else {
            m_frameCountLabel->setText(tr("%1 frames").arg(m_receivedFrameCount));
        }
        m_rowCountLabel->setText(tr("%1 rows").arg(m_receivedFrameCount));
        m_filterLabel->setText(
            tr("Filter %1/%2").arg(m_receivedFrameCount).arg(m_receivedFrameCount));
        return;
    }
    QWidget *active = m_editorArea->currentWidget();
    int total = traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(active)).toBool()
        ? traceQuery(QStringLiteral("frameCount"), QVariant::fromValue(active)).toInt()
        : 0;
    m_frameCountLabel->setText(tr("%1 frames").arg(total));
    m_rowCountLabel->setText(tr("%1 rows").arg(total));
    m_filterLabel->setText(tr("Filter %1/%2").arg(total).arg(total));
}

void MainWindow::updateActions()
{
    bool hasFile = m_player->isLoaded();
    bool playing = m_player->isPlaying();
    m_playAction->setEnabled(hasFile && !playing);
    m_pauseAction->setEnabled(playing);
    m_stopAction->setEnabled(hasFile);
    m_recordAction->setChecked(m_recording);
    transceiveInvoke(QStringLiteral("setPlayerLoaded"), QVariantList{ hasFile, playing });
}

