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
            this, "录制文件", defaultName, CanFileIO::writableFileFilters());
        if (path.isEmpty()) {
            m_recordAction->setChecked(false);
            return;
        }
        if (!CanFileIOFactory::canWrite(QFileInfo(path).suffix())) {
            QMessageBox::warning(this, "录制",
                "不支持的录制格式: ." + QFileInfo(path).suffix() + "\n请使用 ASC 或 CSV 格式。");
            m_recordAction->setChecked(false);
            return;
        }
        if (!m_recorder->start(path)) {
            QMessageBox::warning(this, "录制", "无法创建文件: " + path +
                "\n请检查路径是否有效、磁盘空间是否足够。");
            m_recordAction->setChecked(false);
            m_bottomPanel->addProblem(1, "Recorder", "无法创建录制文件: " + path);
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
        this, "打开文件", {},
        CanFileIO::allFileFilters(false) + ";;DBC 文件 (*.dbc);;所有文件 (*.*)");
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (fi.suffix().toLower() == "dbc") {
        if (!m_dbcManager->loadDbc(path))
            m_bottomPanel->addProblem(1, "DBC", "加载失败: " + path);
    } else {
        if (!m_player->load(path)) {
            QMessageBox::warning(this, "打开", "无法加载: " + path);
            m_bottomPanel->addProblem(1, "Player", "无法加载: " + path);
            return;
        }
        // 清除所有 Trace 和 Graphic 视图（经模块，拆分方案 B5）
        graphicInvoke(QStringLiteral("clearDataAll"));
        traceInvoke(QStringLiteral("clearTraceAll"));
        m_bottomPanel->appendOutput(QString("已加载: %1 (%2 帧, %3s)")
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
        this, QStringLiteral("导入日志文件"), {},
        filters.join(QStringLiteral(";;")));
    if (path.isEmpty()) return;

    auto importer = FileImportFactory::create(path);
    if (!importer) {
        QMessageBox::warning(this, QStringLiteral("导入"), QStringLiteral("不支持的文件格式"));
        return;
    }

    // 进度对话框（500ms 后显示，避免小文件闪烁）
    QProgressDialog progress(QStringLiteral("正在导入..."), QStringLiteral("取消"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    auto frames = importer->importFile(path, [&progress](double pct) {
        progress.setValue(static_cast<int>(pct * 100));
        QApplication::processEvents();
    });

    if (progress.wasCanceled()) return;

    if (frames.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("导入"),
            QStringLiteral("文件为空或解析失败"));
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
    m_bottomPanel->appendOutput(QString("导入完成: %1 (%2 帧, 格式: %3)")
        .arg(fi.fileName()).arg(frames.size()).arg(importer->formatName()));
    m_frameCountLabel->setText(QString::number(count) + QStringLiteral(" 帧"));
    m_rowCountLabel->setText(QString::number(count) + QStringLiteral("行"));
    m_statusLabel->setText(QString("已导入 %1").arg(fi.fileName()));
}

void MainWindow::onAutoScrollToggled(bool on)
{
    m_autoScroll = on;
    // 同步所有 Trace 页滚动状态（经 trace 模块，拆分方案 B5）
    traceInvoke(QStringLiteral("setAutoScroll"), on);
}


// ============================================================
//  右侧面板快捷按钮
// ============================================================

void MainWindow::onQuickRecord()
{
    if (!m_recording)
        onRecord();
}

void MainWindow::onQuickStopRecord()
{
    if (m_recording)
        m_recorder->stop();
}

void MainWindow::onQuickConnect()
{
    // 硬件设备优先连接；未配置真实设备时不再隐式启动模拟器（防混淆），
    // 改为输出引导——模拟器须由用户显式连接（设备连接页 / 模拟器开关）
    if (m_deviceManager->isRealDevice() && !m_deviceManager->isRunning()) {
        m_deviceManager->start();
    } else if (!m_simulator->isRunning() && !m_deviceManager->isRealDevice()) {
        m_bottomPanel->appendOutput(
            QStringLiteral("未配置真实设备：请在设备面板选择并连接硬件；"
                           "如需模拟数据，请显式连接 openbus 模拟器"));
    }
}

void MainWindow::onQuickDisconnect()
{
    m_simulator->stop();
    m_deviceManager->stop();
    m_connLabel->setText("未连接");
    m_bottomPanel->appendOutput("设备已断开");
}

void MainWindow::onAiMessageSent(const QString &text)
{
    // 简单 AI 回复
    m_rightPanel->appendAiMessage("AI", "收到: " + text + "\n(AI 分析功能待实现)");
}

// ============================================================
//  命令行
// ============================================================

void MainWindow::onCommandEntered(const QString &cmd)
{
    processCommand(cmd);
}

void MainWindow::processCommand(const QString &cmd)
{
    auto *out = m_bottomPanel;
    if (cmd == "help" || cmd == "?") {
        out->appendTerminal("可用命令:");
        out->appendTerminal("  help        - 显示帮助");
        out->appendTerminal("  clear       - 清空 Trace");
        out->appendTerminal("  sim on      - 启动模拟器");
        out->appendTerminal("  sim off     - 停止模拟器");
        out->appendTerminal("  dev on      - 启动硬件设备");
        out->appendTerminal("  dev off     - 停止硬件设备");
        out->appendTerminal("  record <file> - 开始录制");
        out->appendTerminal("  stop        - 停止录制/回放");
        out->appendTerminal("  play        - 播放");
        out->appendTerminal("  filter <expr> - 设置过滤器");
        out->appendTerminal("  stats       - 显示统计");
        out->appendTerminal("  load <file> - 加载 DBC/录制文件");
    } else if (cmd == "clear") {
        onClear();
        out->appendTerminal("已清空");
    } else if (cmd == "sim on") {
        m_simulator->start();
        out->appendTerminal("模拟器已启动");
    } else if (cmd == "sim off") {
        m_simulator->stop();
        out->appendTerminal("模拟器已停止");
    } else if (cmd == "dev on") {
        m_deviceManager->start();
        out->appendTerminal("硬件设备已启动");
    } else if (cmd == "dev off") {
        m_deviceManager->stop();
        out->appendTerminal("硬件设备已停止");
    } else if (cmd.startsWith("record ")) {
        QString path = cmd.mid(7).trimmed();
        if (m_recorder->start(path))
            out->appendTerminal("录制开始: " + path);
        else
            out->appendTerminal("录制失败: " + path);
    } else if (cmd == "stop") {
        if (m_recording) m_recorder->stop();
        m_player->stop();
        out->appendTerminal("已停止");
    } else if (cmd == "play") {
        onPlay();
        out->appendTerminal("开始播放");
    } else if (cmd.startsWith("filter ")) {
        QString expr = cmd.mid(7).trimmed();
        // 终端 filter 命令：作用于当前 Trace 页（经模块接口，模块内同步行编辑框）
        QWidget *active = m_editorArea->currentWidget();
        if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(active)).toBool())
            traceInvoke(QStringLiteral("setFilterExpression"),
                        QVariantList{ QVariant::fromValue(active), expr });
        out->appendTerminal("过滤: " + expr);
    } else if (cmd == "stats") {
        updateStatistics();
        QWidget *active = m_editorArea->currentWidget();
        int total = traceQuery(QStringLiteral("frameCount"),
                               QVariant::fromValue(active)).toInt();
        out->appendTerminal(QString("总帧数: %1").arg(total));
    } else if (cmd.startsWith("load ")) {
        QString path = cmd.mid(5).trimmed();
        QFileInfo fi(path);
        if (fi.suffix().toLower() == "dbc") {
            m_dbcManager->loadDbc(path);
        } else if (CanFileIOFactory::canRead(fi.suffix())) {
            if (m_player->load(path)) {
                // 清除所有 Trace 数据（原逐标签页 qobject_cast 内联逻辑，
                // 经模块 clearTraceAll 广播）
                traceInvoke(QStringLiteral("clearTraceAll"));
                out->appendTerminal("已加载报文文件: " + fi.fileName());
                updateActions();
            } else {
                out->appendTerminal("加载失败: " + fi.fileName());
            }
        } else {
            out->appendTerminal("不支持的格式: ." + fi.suffix());
        }
    } else {
        out->appendTerminal("未知命令: " + cmd + " (输入 help 查看帮助)");
    }
}

// ============================================================
//  统计 & 状态
// ============================================================

void MainWindow::updateStatistics()
{
    if (m_measurementRunning) {
        // 测量运行中：显示统一计数
        if (m_player->isLoaded()) {
            int total = m_player->totalFrames();
            m_frameCountLabel->setText(
                QString::number(m_receivedFrameCount) + " / " +
                QString::number(total) + " 帧");
        } else {
            m_frameCountLabel->setText(QString::number(m_receivedFrameCount) + " 帧");
        }
        m_rowCountLabel->setText(QString::number(m_receivedFrameCount) + "行");
        m_filterLabel->setText(
            QString("过滤%1/%2").arg(m_receivedFrameCount).arg(m_receivedFrameCount));
        return;
    }
    // 非测量状态：显示当前 Trace 标签页统计（经 trace 模块查询，拆分方案 B5）
    QWidget *active = m_editorArea->currentWidget();
    int total = traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(active)).toBool()
        ? traceQuery(QStringLiteral("frameCount"), QVariant::fromValue(active)).toInt()
        : 0;
    m_frameCountLabel->setText(QString::number(total) + " 帧");
    m_rowCountLabel->setText(QString::number(total) + "行");
    m_filterLabel->setText(QString("过滤%1/%2").arg(total).arg(total));
}

void MainWindow::updateActions()
{
    // DEF-01 Fix: Guard against null m_player during early construction
    if (!m_player || !m_playAction) {
        qWarning("MainWindow: Skipping action update (m_player=%p, m_playAction=%p)", 
                 static_cast<void*>(m_player), static_cast<void*>(m_playAction));
        return;
    }
    
    bool hasFile = m_player->isLoaded();
    bool playing = m_player->isPlaying();
    m_playAction->setEnabled(hasFile && !playing);
    m_pauseAction->setEnabled(playing);
    m_stopAction->setEnabled(hasFile);
    m_recordAction->setChecked(m_recording);
    transceiveInvoke(QStringLiteral("setPlayerLoaded"), QVariantList{ hasFile, playing });
}

