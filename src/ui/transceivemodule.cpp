#include "transceivemodule.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>

#include "signalsendtab.h"
#include "playbacktab.h"
#include "offlineanalysistab.h"
#include "recordtab.h"
#include "dbcimportdialog.h"
#include "core/canframe.h"
#include "core/player.h"
#include "core/recorder.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/dbcmanager.h"
#include "core/triggerrecorder.h"
#include "core/canfileio/canfileio_factory.h"

// openbus_transceive.dll 唯一显式导出的符号（拆分方案 §4.2 决策 1）。
// 工厂按模块唯一命名（openbus_create<Xxx>Module），避免多 DLL 链接期同名冲突。
extern "C" IBusinessModule *openbus_createTransceiveModule()
{
    return new TransceiveModule;
}

QString TransceiveModule::id() const
{
    return QStringLiteral("transceive");
}

QString TransceiveModule::title() const
{
    return QStringLiteral("收发");
}

QIcon TransceiveModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/send.svg"));
}

QStringList TransceiveModule::pages() const
{
    return { QStringLiteral("signalsend"), QStringLiteral("playback"),
             QStringLiteral("offlineanalysis"), QStringLiteral("record") };
}

QWidget *TransceiveModule::createWidget(ShellContext &ctx)
{
    return createPage(QStringLiteral("signalsend"), ctx);
}

QWidget *TransceiveModule::createPage(const QString &pageId, ShellContext &ctx)
{
    // 已有页面直接复用（标签页关闭后从 m_pages 移除）
    if (QWidget *existing = m_pages.value(pageId))
        return existing;

    m_ctx = ctx;   // 保存最近上下文（appendOutput/shellInvoke 等回调）

    QWidget *page = nullptr;
    if (pageId == QStringLiteral("signalsend"))
        page = createSendPage(ctx);
    else if (pageId == QStringLiteral("playback"))
        page = createPlaybackPage(ctx);
    else if (pageId == QStringLiteral("offlineanalysis"))
        page = createOfflinePage(ctx);
    else if (pageId == QStringLiteral("record"))
        page = createRecordPage(ctx);
    if (!page)
        return nullptr;

    m_pages.insert(pageId, page);
    // 标签页关闭 → widget 销毁 → 从页面表移除（下次 createPage 重新创建）
    QObject::connect(page, &QObject::destroyed, page, [this, pageId]() {
        m_pages.remove(pageId);
    });
    return page;
}

void TransceiveModule::invoke(const QString &action, const QVariant &arg)
{
    if (action == QStringLiteral("setRecording")) {
        if (QWidget *w = m_pages.value(QStringLiteral("record")))
            if (auto *tab = qobject_cast<RecordTab *>(w))
                tab->setRecording(arg.toBool());
    } else if (action == QStringLiteral("setFileInfo")) {
        if (QWidget *w = m_pages.value(QStringLiteral("playback"))) {
            auto *tab = qobject_cast<PlaybackTab *>(w);
            const QVariantList list = arg.toList();
            if (tab && list.size() == 3)
                tab->setFileInfo(list.at(0).toString(), list.at(1).toInt(),
                                 list.at(2).toDouble());
        }
    } else if (action == QStringLiteral("setProgress")) {
        if (QWidget *w = m_pages.value(QStringLiteral("playback"))) {
            auto *tab = qobject_cast<PlaybackTab *>(w);
            const QVariantList list = arg.toList();
            if (tab && list.size() == 4)
                tab->setProgress(list.at(0).toInt(), list.at(1).toInt(),
                                 list.at(2).toDouble(), list.at(3).toDouble());
        }
    } else if (action == QStringLiteral("setPlayerLoaded")) {
        if (QWidget *w = m_pages.value(QStringLiteral("playback"))) {
            auto *tab = qobject_cast<PlaybackTab *>(w);
            const QVariantList list = arg.toList();
            if (tab && list.size() == 2)
                tab->setPlayerLoaded(list.at(0).toBool(), list.at(1).toBool());
        }
    } else if (action == QStringLiteral("addOfflineFiles")) {
        // 工程恢复：离线分析页不存在时静默忽略（与其他 action 约定一致）
        if (QWidget *w = m_pages.value(QStringLiteral("offlineanalysis")))
            if (auto *tab = qobject_cast<OfflineAnalysisTab *>(w))
                tab->addFiles(arg.toStringList());
    } else if (action == QStringLiteral("loadRecordConfig")) {
        if (QWidget *w = m_pages.value(QStringLiteral("record")))
            if (auto *tab = qobject_cast<RecordTab *>(w))
                tab->loadConfig(arg.toMap());
    } else if (action == QStringLiteral("loadSendEntries")) {
        if (QWidget *w = m_pages.value(QStringLiteral("signalsend")))
            if (auto *tab = qobject_cast<SignalSendTab *>(w))
                tab->loadEntries(arg.toList());
    } else if (action == QStringLiteral("loadPlaybackConfig")) {
        if (QWidget *w = m_pages.value(QStringLiteral("playback")))
            if (auto *tab = qobject_cast<PlaybackTab *>(w))
                tab->loadConfig(arg.toMap());
    } else if (action == QStringLiteral("retranslate")) {
        for (auto it = m_pages.constBegin(); it != m_pages.constEnd(); ++it) {
            if (auto *tab = qobject_cast<PlaybackTab *>(it.value()))
                tab->retranslateUi();
            else if (auto *tab = qobject_cast<OfflineAnalysisTab *>(it.value()))
                tab->retranslateUi();
            else if (auto *tab = qobject_cast<RecordTab *>(it.value()))
                tab->retranslateUi();
        }
    }
}

QVariant TransceiveModule::query(const QString &what, const QVariant &)
{
    if (what == QStringLiteral("offlineFiles")) {
        if (QWidget *w = m_pages.value(QStringLiteral("offlineanalysis")))
            if (auto *tab = qobject_cast<OfflineAnalysisTab *>(w))
                return tab->filePaths();
    } else if (what == QStringLiteral("recordConfig")) {
        if (QWidget *w = m_pages.value(QStringLiteral("record")))
            if (auto *tab = qobject_cast<RecordTab *>(w))
                return tab->configMap();
    } else if (what == QStringLiteral("sendEntries")) {
        if (QWidget *w = m_pages.value(QStringLiteral("signalsend")))
            if (auto *tab = qobject_cast<SignalSendTab *>(w))
                return tab->exportEntries();
    } else if (what == QStringLiteral("playbackConfig")) {
        if (QWidget *w = m_pages.value(QStringLiteral("playback")))
            if (auto *tab = qobject_cast<PlaybackTab *>(w))
                return tab->configMap();
    }
    return {};
}

// ============================================================
//  发送页（原 MainWindow::setupSendTab，依赖经 ShellContext）
// ============================================================

QWidget *TransceiveModule::createSendPage(ShellContext &ctx)
{
    auto *tab = new SignalSendTab(ctx.mainWindow);
    tab->setDbcManager(ctx.dbcManager);

    // Unified Tx loopback → CaptureLog / SampleStore / Flow (same as plugin send).
    // Do NOT use appendFrames here: that targets TraceTab only and bypasses the
    // live CaptureLog camera path (and previously passed SignalSendTab by mistake).
    auto injectTxLoopback = [this](const CanFrame &frame) {
        if (m_ctx.shellInvoke) {
            QVariantList frameList;
            frameList.append(QVariant::fromValue(frame));
            m_ctx.shellInvoke(QStringLiteral("ingestTxEcho"), frameList);
        }
    };

    auto sendFrame = [this, injectTxLoopback](quint32 id, const QByteArray &data) {
        CanFrame frame;
        frame.id = id;
        frame.dlc = CanFrame::lengthToDlc(data.size());
        frame.data = data;
        frame.direction = CanFrame::Tx;

        bool success = false;
        if (m_ctx.deviceManager) {
            CanFrame echo;
            success = m_ctx.deviceManager->sendFrame(frame, &echo);
            if (success)
                injectTxLoopback(echo);
        }
        return success;
    };

    // Single-frame send
    QObject::connect(tab, &SignalSendTab::sendSingleRequested,
                     tab, [this, sendFrame](quint32 id, const QByteArray &data) {
        if (sendFrame(id, data)) {
            if (m_ctx.appendOutput)
                m_ctx.appendOutput(QStringLiteral("Sent: ID=0x%1, DLC=%2")
                                       .arg(id, 0, 16).toUpper().arg(data.size()));
        } else {
            if (m_ctx.appendOutput)
                m_ctx.appendOutput(
                    QStringLiteral("Send failed (device not running): ID=0x%1")
                        .arg(id, 0, 16).toUpper());
        }
    });

    // Row send (one-shot or periodic) — timers owned by this module
    QObject::connect(tab, &SignalSendTab::sendRowRequested,
                     tab, [this, sendFrame, tab, injectTxLoopback](int row, quint32 id, const QByteArray &data,
                                            int period, int count) {
        if (sendFrame(id, data)) {
            if (m_ctx.appendOutput)
                m_ctx.appendOutput(QStringLiteral("Send row %1: ID=0x%2, DLC=%3")
                                       .arg(row + 1).arg(id, 0, 16).toUpper().arg(data.size()));
        } else {
            if (m_ctx.appendOutput)
                m_ctx.appendOutput(
                    QStringLiteral("Send failed (device not running): ID=0x%1")
                        .arg(id, 0, 16).toUpper());
        }

        if (period > 0) {
            auto it = m_periodicSenders.find(row);
            if (it != m_periodicSenders.end()) {
                it.value()->stop();
                it.value()->deleteLater();
                m_periodicSenders.erase(it);
            }

            auto *timer = new QTimer(tab);
            timer->setInterval(period);
            int remaining = count;  // 0 = infinite
            QObject::connect(timer, &QTimer::timeout, tab,
                    [this, id, data, row, count, timer, remaining, injectTxLoopback]() mutable {
                CanFrame f;
                f.id = id;
                f.dlc = CanFrame::lengthToDlc(data.size());
                f.data = data;
                f.direction = CanFrame::Tx;
                if (m_ctx.deviceManager) {
                    CanFrame echo;
                    if (m_ctx.deviceManager->sendFrame(f, &echo))
                        injectTxLoopback(echo);
                }

                if (count > 0) {
                    --remaining;
                    if (remaining <= 0) {
                        timer->stop();
                        timer->deleteLater();
                        m_periodicSenders.remove(row);
                        if (m_ctx.appendOutput)
                            m_ctx.appendOutput(
                                QStringLiteral("Row %1 periodic send done (%2 times)")
                                    .arg(row + 1).arg(count));
                    }
                }
            });
            timer->start();
            m_periodicSenders[row] = timer;
        }
    });

    // 停止单行
    QObject::connect(tab, &SignalSendTab::stopRowRequested,
                     tab, [this](int row) {
        auto it = m_periodicSenders.find(row);
        if (it != m_periodicSenders.end()) {
            it.value()->stop();
            it.value()->deleteLater();
            m_periodicSenders.erase(it);
            if (m_ctx.appendOutput)
                m_ctx.appendOutput(QString("停止行%1 周期发送").arg(row + 1));
        }
    });

    // 全部停止（安全网）
    QObject::connect(tab, &SignalSendTab::stopAllRequested, tab, [this]() {
        for (auto *t : m_periodicSenders) {
            t->stop();
            t->deleteLater();
        }
        m_periodicSenders.clear();
    });

    return tab;
}

// ============================================================
//  回放页（原 MainWindow::setupPlaybackTab）
// ============================================================

QWidget *TransceiveModule::createPlaybackPage(ShellContext &ctx)
{
    auto *tab = new PlaybackTab(ctx.mainWindow);

    // 回放链路走壳（"play" 触发壳的完整未加载兜底逻辑：收集帧/打开文件）
    QObject::connect(tab, &PlaybackTab::playRequested, tab, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("play"), {});
    });
    QObject::connect(tab, &PlaybackTab::pauseRequested, tab, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("pause"), {});
    });
    QObject::connect(tab, &PlaybackTab::stopRequested, tab, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("stop"), {});
    });
    QObject::connect(tab, &PlaybackTab::speedChanged, tab, [this](double speed) {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("setSpeed"), speed);
    });

    // seek / loop 纯 data 层操作，模块直连回放器
    QObject::connect(tab, &PlaybackTab::seekChanged, tab, [this](double ratio) {
        if (m_ctx.player && m_ctx.player->isLoaded())
            m_ctx.player->seekTo(ratio * m_ctx.player->totalTime());
    });
    QObject::connect(tab, &PlaybackTab::loopToggled, tab, [this](bool on) {
        if (m_ctx.player) m_ctx.player->setLoop(on);
    });

    // Trace 自动滚动是壳的全局设置
    QObject::connect(tab, &PlaybackTab::autoScrollToggled, tab, [this](bool on) {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("setAutoScroll"), on);
    });

    // 文件加载：模块内直载回放器 + 通知壳清视图/刷新动作
    QObject::connect(tab, &PlaybackTab::fileLoaded, tab,
                     [this, tab](const QString &path) {
        QFileInfo fi(path);
        if (!m_ctx.player || !m_ctx.player->load(path)) {
            QMessageBox::warning(m_ctx.mainWindow, QStringLiteral("回放"),
                                 QStringLiteral("无法加载: ") + path);
            if (m_ctx.addProblem)
                m_ctx.addProblem(1, QStringLiteral("Player"),
                                 QStringLiteral("无法加载: ") + path);
            return;
        }
        // 清除所有 Trace 和 Graphic 视图（壳编排）
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("clearTraceGraphic"), {});
        if (m_ctx.appendOutput)
            m_ctx.appendOutput(QString("已加载: %1 (%2 帧, %3s)")
                                   .arg(fi.fileName()).arg(m_ctx.player->totalFrames())
                                   .arg(m_ctx.player->totalTime(), 0, 'f', 2));
        tab->setFileInfo(fi.fileName(), m_ctx.player->totalFrames(),
                         m_ctx.player->totalTime());
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("updateActions"), {});
    });

    return tab;
}

// ============================================================
//  离线分析页（纯文件列表管理，无装配逻辑）
// ============================================================

QWidget *TransceiveModule::createOfflinePage(ShellContext &ctx)
{
    // 离线分析标签页是纯文件列表管理，不直接加载文件；
    // 文件加载由 Flow 界面点击"开始"时统一处理（measurementToggled）
    return new OfflineAnalysisTab(ctx.mainWindow);
}

// ============================================================
//  录制页（原 MainWindow::setupRecordTab + onTriggerRecording）
// ============================================================

QWidget *TransceiveModule::createRecordPage(ShellContext &ctx)
{
    auto *tab = new RecordTab(ctx.mainWindow);

    QObject::connect(tab, &RecordTab::recordToggled, tab, [this, tab](bool on) {
        if (on) {
            // 使用 RecordTab 面板设置自动生成文件路径
            QString dir = tab->directory();
            if (dir.isEmpty()) dir = ".";
            QDir().mkpath(dir);
            QString prefix = tab->prefix();
            if (prefix.isEmpty()) prefix = "rec";
            QString fmt = tab->format();
            if (!CanFileIOFactory::canWrite(fmt)) fmt = "asc";
            QString fileName = prefix + "_" +
                QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") +
                "." + fmt;
            QString path = QDir(dir).filePath(fileName);

            if (!m_ctx.recorder || !m_ctx.recorder->start(path)) {
                QMessageBox::warning(m_ctx.mainWindow, QStringLiteral("录制"),
                    QStringLiteral("无法创建文件: ") + path +
                    QStringLiteral("\n请检查路径是否有效、磁盘空间是否足够。"));
                tab->setRecording(false);
                if (m_ctx.addProblem)
                    m_ctx.addProblem(1, QStringLiteral("Recorder"),
                                     QStringLiteral("无法创建录制文件: ") + path);
                return;
            }
            if (m_ctx.appendOutput)
                m_ctx.appendOutput(QStringLiteral("开始录制: ") + path);
        } else {
            if (m_ctx.recorder) m_ctx.recorder->stop();
        }
    });

    // 暂停/恢复录制
    QObject::connect(tab, &RecordTab::pauseRequested, tab, [this](bool paused) {
        if (!m_ctx.recorder) return;
        if (paused) {
            m_ctx.recorder->pause();
            if (m_ctx.appendOutput) m_ctx.appendOutput(QStringLiteral("录制已暂停"));
        } else {
            m_ctx.recorder->resume();
            if (m_ctx.appendOutput) m_ctx.appendOutput(QStringLiteral("录制已恢复"));
        }
    });

    // 触发录制（原 MainWindow::onTriggerRecording；TriggerRecorder 归模块所有）
    QObject::connect(tab, &RecordTab::triggerRecordingRequested, tab,
            [this, tab](const QString &dir, const QString &prefix, const QString &format,
                   bool splitBySize, int sizeMb, bool splitByTime, int timeSec,
                   bool ringMode, int maxFiles,
                   const QString &triggerExpr, double preTriggerSec, double postTriggerSec,
                   bool repeatTrigger) {
        if (!m_triggerRecorder) {
            m_triggerRecorder = new TriggerRecorder(tab);
            // DEF-08 字符串信号：simulator/deviceManager 定义于 data.dll
            QObject::connect(m_ctx.simulator, SIGNAL(framesGenerated(QVector<CanFrame>)),
                             m_triggerRecorder, SLOT(onFrames(QVector<CanFrame>)));
            QObject::connect(m_ctx.deviceManager, SIGNAL(framesGenerated(QVector<CanFrame>)),
                             m_triggerRecorder, SLOT(onFrames(QVector<CanFrame>)));
        }

        TriggerRecorder::Config config;
        config.logConfig.directory = dir;
        config.logConfig.prefix = prefix;
        config.logConfig.format = format;
        config.logConfig.splitBySize = splitBySize;
        config.logConfig.maxSizeBytes = static_cast<quint64>(sizeMb) * 1024 * 1024;
        config.logConfig.splitByTime = splitByTime;
        config.logConfig.maxTimeSeconds = static_cast<double>(timeSec);
        config.logConfig.ringMode = ringMode;
        config.logConfig.maxFiles = maxFiles;
        config.triggerExpr = triggerExpr;
        config.preTriggerSeconds = preTriggerSec;
        config.postTriggerSeconds = postTriggerSec;
        config.repeatTrigger = repeatTrigger;

        if (!m_triggerRecorder->isRunning()) {
            if (!m_triggerRecorder->start(config)) {
                QMessageBox::warning(m_ctx.mainWindow, QStringLiteral("触发录制"),
                    QStringLiteral("触发条件表达式编译失败，请检查表达式语法。"));
                return;
            }
            if (m_ctx.shellInvoke)
                m_ctx.shellInvoke(QStringLiteral("statusMessage"),
                                  QStringLiteral("触发录制中... 等待触发条件"));
        } else {
            m_triggerRecorder->stop();
            if (m_ctx.shellInvoke)
                m_ctx.shellInvoke(QStringLiteral("statusMessage"),
                                  QStringLiteral("触发录制已停止"));
        }
    });

    // 触发录制停止
    QObject::connect(tab, &RecordTab::triggerRecordingStopped, tab, [this]() {
        if (m_triggerRecorder)
            m_triggerRecorder->stop();
    });

    return tab;
}
