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
#include "ui/measurementsetupview.h"  // Flow 画布视图（双击 Filter/CAN parser 块时调用）
#include "ui/dbcsignalpickerdialog.h"  // Graphic 侧栏「添加信号」弹窗（DBC 信号搜索/多选）
#include "ui/watcherview.h"            // Watcher 观测页（喂帧/复位直调，doc/Watcher 方案.md）
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
#include "utils/logging.h"
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
//  MainWindow 数据流编排（B6 拆分自 mainwindow.cpp）
//  帧接收分发 / Trace↔Graphic 联动 / 回放进度 / DBC 信号联动 /
//  测量启停门控（onMeasurementToggled/onModuleToggled/实例开合）
// ============================================================

// ============================================================
//  数据流
// ============================================================

void MainWindow::onFrameReceived(const CanFrame &frame)
{
    // 测量未运行时直接断流 — 不再向任何数据块分发帧
    if (!m_measurementRunning)
        return;

    // 发送到所有启用的 Graphic 视图（含 DataWindow），仅向运行中的 Trace
    // 标签页追加帧（经模块分发，拆分方案 B5）
    graphicInvoke(QStringLiteral("onFrame"), QVariant::fromValue(frame));
    traceInvoke(QStringLiteral("onFrame"), QVariant::fromValue(frame));
    // Flow 页接收帧（测量统计经 flow 模块分发，拆分方案 B4）
    flowInvoke(QStringLiteral("onFrame"), QVariant::fromValue(frame));
    if (m_recording)
        m_recorder->recordFrame(frame);
    // 发送到总线统计引擎
    if (m_busStats)
        m_busStats->onFrame(frame);
    // 发送到 I/O Graph
    if (m_ioGraph)
        m_ioGraph->onFrame(frame);
    // 发送到 Watcher 观测页（doc/Watcher方案.md：最新帧缓存 + 懒解码，500ms 刷新）
    if (m_watcherView)
        m_watcherView->onFrame(frame);
    // 更新状态栏帧数（使用独立计数器，不依赖当前标签页类型）
    m_receivedFrameCount++;
    if (m_player->isLoaded()) {
        // 文件回放模式：显示 "当前 / 总数"
        int total = m_player->totalFrames();
        m_frameCountLabel->setText(
            QString::number(m_receivedFrameCount) + " / " +
            QString::number(total) + " 帧");
        m_rowCountLabel->setText(QString::number(m_receivedFrameCount) + "行");
    } else {
        // 硬件实时模式：仅显示当前计数
        m_frameCountLabel->setText(QString::number(m_receivedFrameCount) + " 帧");
        m_rowCountLabel->setText(QString::number(m_receivedFrameCount) + "行");
    }

    // 通知插件系统
    if (m_pluginManager)
        m_pluginManager->onFrameReceived(frame);
}

void MainWindow::onFramePlayed(const CanFrame &frame)
{
    onFrameReceived(frame);
}

// ============================================================
//  Trace/Graphic 联动（B5：TraceModule 经 shellInvoke 回调壳编排）
// ============================================================

void MainWindow::onFrameDoubleClicked(const CanFrame &frame)
{
    // 1. 当前 Trace 页过滤栏设置过滤（经 trace 模块）
    QString filter = QString("id == %1").arg(CanUtils::formatId(frame.id, frame.extended));
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        QWidget *cur = tabs->currentWidget();
        if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(cur)).toBool()) {
            traceInvoke(QStringLiteral("setFilterExpression"),
                        QVariantList{ QVariant::fromValue(cur), filter, false });
        }
    }

    // 2. 添加 raw byte0 信号到当前或最后的 Graphic 视图（经 graphic 模块）
    QWidget *targetGv = resolveGraphicTarget();
    if (!targetGv)
        return;

    // 原始位流信号：byte0 按 Intel/8bit/1.0/0.0 解析（与旧 GraphicView 行为一致）
    DbcSignal rawSig;
    rawSig.name = QString("ID_%1[0]").arg(frame.id, 0, 16).toUpper();
    rawSig.startBit = 0;
    rawSig.bitLength = 8;
    rawSig.littleEndian = true;
    rawSig.factor = 1.0;
    rawSig.offset = 0.0;
    graphicInvoke(QStringLiteral("addSignal"),
                  QVariantList{ QVariant::fromValue(targetGv),
                                buildSignalMap(frame.id & 0x1FFFFFFF, frame.extended,
                                               rawSig.name, rawSig) });

    // 3. 切换到 Graphic 标签页
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->widget(i) == targetGv) {
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(tabs->tabText(i));
                break;
            }
        }
    }
}

void MainWindow::onFrameAddToGraphic(const CanFrame &frame)
{
    // Trace 右键"添加信号到 Graphic"：查该帧对应的 DBC 报文，全部信号
    // 添加到当前或最后的 Graphic 视图（原 setupTraceTab 内联逻辑，B5 迁壳槽）
    const DbcMessage *msg = m_dbcManager->findMessage(frame.id);
    if (!msg) {
        m_bottomPanel->appendOutput(
            QStringLiteral("未找到 ID=0x%1 对应的 DBC 报文定义")
                .arg(frame.id, 0, 16).toUpper());
        return;
    }
    QWidget *targetGv = resolveGraphicTarget();
    if (!targetGv)
        return;
    QVariantList sigMaps;
    for (const auto &sig : msg->signalList)
        sigMaps.append(buildSignalMap(frame.id, frame.extended, sig.name, sig));
    graphicInvoke(QStringLiteral("addSignals"),
                  QVariantList{ QVariant::fromValue(targetGv), sigMaps });
    m_bottomPanel->appendOutput(
        QStringLiteral("已添加 %1 个信号到 Graphic (ID=0x%2)")
            .arg(msg->signalList.size()).arg(frame.id, 0, 16).toUpper());
}

void MainWindow::onGraphicAddSignalRequested()
{
    // Graphic 侧栏「添加信号」：DBC 信号选择弹窗（搜索 / 树形浏览 / Ctrl+Shift
    // 多选）→ resolveGraphicTarget（无则新建）→ graphic 模块 addSignals 批量添加
    if (m_dbcManager->files().isEmpty()) {
        m_bottomPanel->appendOutput(
            QStringLiteral("尚未加载 DBC 数据库 — 请先在数据库面板加载文件"));
        return;
    }

    DbcSignalPickerDialog dlg(m_dbcManager, QString(), this);  // 缺省标题「添加信号到 Graphic」
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto picked = dlg.pickedSignals();
    if (picked.isEmpty())
        return;

    QWidget *targetGv = resolveGraphicTarget();
    if (!targetGv)
        return;

    QVariantList sigMaps;
    QSet<QString> msgSources;   // 来源报文统计（输出提示用）
    for (const auto &p : picked) {
        sigMaps.append(buildSignalMap(p.canId, p.extended, p.signal.name, p.signal));
        msgSources.insert(QStringLiteral("%1::%2").arg(p.fileName, p.messageName));
    }
    graphicInvoke(QStringLiteral("addSignals"),
                  QVariantList{ QVariant::fromValue(targetGv), sigMaps });

    // 切换到目标 Graphic 标签页（与 onFrameDoubleClicked 行为对齐）
    if (auto *tabs = m_editorArea->activeTabWidget()) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->widget(i) == targetGv) {
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(tabs->tabText(i));
                break;
            }
        }
    }

    m_bottomPanel->appendOutput(
        QStringLiteral("已添加 %1 个信号到 Graphic（来自 %2 个报文）")
            .arg(picked.size()).arg(msgSources.size()));
}

// ============================================================
//  回放
// ============================================================

void MainWindow::onPlayerProgress(int cur, int total, double curTime, double totalTime)
{
    transceiveInvoke(QStringLiteral("setProgress"),
                     QVariantList{ cur, total, curTime, totalTime });
    if (totalTime > 0)
        m_timeLabel->setText(QString::number(curTime, 'f', 3) + "s / " +
                              QString::number(totalTime, 'f', 3) + "s");
    else
        m_timeLabel->setText(QString::number(curTime, 'f', 3) + "s");
}

void MainWindow::onPlayerStateChanged(bool playing)
{
    updateActions();
    m_statusLabel->setText(playing ? "回放中..." : "已暂停");
}

void MainWindow::onPlayerFinished()
{
    m_statusLabel->setText("回放完成");
    updateActions();
    // 离线测量经 Player 回放：文件分析完毕后复位 Flow 页启停按钮，
    // 「开始」恢复可点（再次点击即重新回放）——否则按钮停留在运行态
    if (m_measurementRunning && flowQuery(QStringLiteral("currentSource"))
                                     .toString() == QStringLiteral("file")) {
        m_measurementRunning = false;
        m_bottomPanel->appendOutput("离线分析完成");
        flowInvoke(QStringLiteral("setMeasurementRunning"), false);
    }
}

void MainWindow::onSpeedChanged(double speed)
{
    m_player->setSpeed(speed);
}

void MainWindow::onSeekChanged(double ratio)
{
    if (!m_player->isLoaded()) return;
    m_player->seekTo(ratio * m_player->totalTime());
}

// ============================================================
//  DBC
// ============================================================

void MainWindow::onSignalDoubleClicked(quint32 canId, const QString &signalName)
{
    const DbcMessage *msg = m_dbcManager->findMessage(canId);
    const DbcSignal *sig = msg ? msg->findSignal(signalName) : nullptr;
    if (!sig) return;

    // 添加到当前或最后的 Graphic 视图（经 graphic 模块，拆分方案 B5）
    QWidget *targetGv = resolveGraphicTarget();
    if (!targetGv)
        return;
    graphicInvoke(QStringLiteral("addSignal"),
                  QVariantList{ QVariant::fromValue(targetGv),
                                buildSignalMap(canId, msg && (msg->id > 0x7FF),
                                               signalName, *sig) });

    m_bottomPanel->appendOutput(QString("已添加信号: %1 (ID=0x%2)")
        .arg(signalName).arg(canId, 0, 16).toUpper());
}

void MainWindow::onDbcFileClicked(const QString &fileName)
{
    // DBC 详情页经 DBC 模块创建（拆分方案 B3）：装配在模块内完成，
    // 信号联动经 shellInvoke 回调壳的信号→Graphic / 信号→Trace 编排
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("dbc"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("detail"), fileName, ctx))
            openTab(page, "DBC: " + fileName);
    }
}

void MainWindow::onSignalAddToTrace(quint32 canId, const QString &signalName)
{
    // 查找现有 Trace 实例（经 trace 模块，拆分方案 B5），没有则经模块新建
    QWidget *target = nullptr;
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        QWidget *cur = tabs->currentWidget();
        if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(cur)).toBool()) {
            target = cur;
        } else {
            const auto allTabs = m_editorArea->allTabWidgets();
            for (auto *tw : allTabs) {
                for (int i = tw->count() - 1; i >= 0; --i) {
                    QWidget *w = tw->widget(i);
                    if (traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(w)).toBool()) {
                        target = w;
                        break;
                    }
                }
                if (target) break;
            }
        }
    }
    if (!target) {
        target = createTraceInstance(QString("trace%1").arg(++m_traceCount));
        if (!target)
            return;
    } else {
        // 切换到已有 Trace 标签页
        if (tabs) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->widget(i) == target) {
                    tabs->setCurrentIndex(i);
                    m_tabLabel->setText(tabs->tabText(i));
                    break;
                }
            }
        }
    }

    // 设置过滤器：只显示该 CAN ID 的帧（经 trace 模块；report=true 报告语法错误）
    QString filter = QString("id == 0x%1").arg(canId, 0, 16).toUpper();
    traceInvoke(QStringLiteral("setFilterExpression"),
                QVariantList{ QVariant::fromValue(target), filter, true });

    m_bottomPanel->appendOutput(QString("已添加信号到 Trace: %1 (ID=0x%2)")
        .arg(signalName).arg(canId, 0, 16).toUpper());
}


// ============================================================
//  Flow 编排槽（拆分方案 B4：FlowModule 经 shellInvoke 回调；
//  离线加载/实例门控等跨模块编排在壳侧完成）
// ============================================================

void MainWindow::onMeasurementToggled(bool running)
{
    m_measurementRunning = running;
    if (running) {
        m_receivedFrameCount = 0;  // 重置帧计数器
        // 新测量会话：总线统计引擎与 Watcher 观测数据全部归零重新累计
        // （修复统计跨会话累计的缺陷，doc/Watcher方案.md §4.6）
        if (m_busStats)
            m_busStats->clear();
        if (m_watcherView)
            m_watcherView->clearData();
        m_bottomPanel->appendOutput(" 测量开始");
        const bool hardware = flowQuery(QStringLiteral("currentSource"))
                                  .toString() == QStringLiteral("hardware");
        if (hardware) {
            // 硬件模式：数据源 = 已连接的真实硬件设备
            if (m_deviceManager->isRunning()) {
                // 真实硬件已连接，无需重复启动
            } else if (!m_simulator->isRunning()) {
                // 不隐式启动模拟器（防混淆）：数据源就绪前测量空转，
                // 设备连接后帧自动流入（onFrameReceived 仅门控测量状态）
                m_bottomPanel->appendOutput(
                    QStringLiteral("数据源未就绪：未检测到已连接设备。"
                                   "请到设备连接页连接硬件，或显式连接 openbus 模拟器"));
            }
        } else {
            // 离线分析模式：从离线分析标签页加载所有文件，合并后送入 Player
            m_player->stop();
            // 文件列表经 transceive 模块查询（拆分方案 B2）
            QStringList paths = transceiveQuery(
                QStringLiteral("offlineFiles")).toStringList();

            if (paths.isEmpty()) {
                // 无文件 → 回退到文件选择框
                onOpenFile();
                if (!m_player->isLoaded()) {
                    // 用户取消选择：测量未真正启动，复位 Flow 页按钮状态
                    m_measurementRunning = false;
                    flowInvoke(QStringLiteral("setMeasurementRunning"), false);
                    return;
                }
            } else {
                // 逐个文件加载帧并合并
                QVector<CanFrame> allFrames;
                QStringList loadedNames;
                for (const auto &path : paths) {
                    auto reader = CanFileIOFactory::createReader(path);
                    if (!reader || !reader->open(path)) {
                        m_bottomPanel->appendOutput(
                            QStringLiteral("解析失败: %1").arg(QFileInfo(path).fileName()));
                        continue;
                    }
                    QVector<CanFrame> frames;
                    int count = reader->readAll(frames);
                    reader->close();
                    if (count > 0) {
                        allFrames += frames;
                        loadedNames << QFileInfo(path).fileName();
                        m_bottomPanel->appendOutput(
                            QStringLiteral("已加载: %1 (%2 帧)")
                                .arg(QFileInfo(path).fileName()).arg(count));
                    }
                }
                if (allFrames.isEmpty()) {
                    QMessageBox::warning(this, QStringLiteral("离线分析"),
                        QStringLiteral("所有文件解析失败或为空"));
                    // 测量未真正启动：复位 Flow 页按钮状态
                    m_measurementRunning = false;
                    flowInvoke(QStringLiteral("setMeasurementRunning"), false);
                    return;
                }
                // 按时间戳排序合并帧
                std::sort(allFrames.begin(), allFrames.end(),
                          [](const CanFrame &a, const CanFrame &b) {
                              return a.timestamp < b.timestamp;
                          });
                m_player->loadFrames(allFrames);
                m_bottomPanel->appendOutput(
                    QStringLiteral("共加载 %1 个文件, %2 帧")
                        .arg(loadedNames.size()).arg(allFrames.size()));
            }

            // 清除所有 Trace 和 Graphic 视图（经模块，拆分方案 B5）
            graphicInvoke(QStringLiteral("clearDataAll"));
            traceInvoke(QStringLiteral("clearTraceAll"));
            m_player->play();
        }
        // 所有已启用的 Trace 实例自动开始接收数据（遵循 Flow 块使能状态；
        // 经 trace 模块按 id 门控，拆分方案 B5）
        const auto traceIds = m_traceInstances.keys();
        for (const QString &traceId : traceIds) {
            traceInvoke(QStringLiteral("setRunning"),
                        QVariantList{ traceId,
                                      flowQuery(QStringLiteral("isBlockEnabled"), traceId).toBool() });
        }
    } else {
        m_bottomPanel->appendOutput("测量停止");
        m_simulator->stop();
        m_deviceManager->stop();
        m_player->stop();
        traceInvoke(QStringLiteral("setRunningAll"), false);
    }
}

void MainWindow::onModuleToggled(const QString &blockId, const QString &name, bool enabled)
{
    m_bottomPanel->appendOutput(QString("模块 %1 %2")
                                .arg(name).arg(enabled ? "已启用" : "已禁用"));
    // 根据 blockId 控制对应实例的数据接收（经模块，拆分方案 B5）
    if (blockId.startsWith("trace")) {
        traceInvoke(QStringLiteral("setRunning"),
                    QVariantList{ blockId, enabled && m_measurementRunning });
    } else if (blockId.startsWith("graphic")) {
        if (QWidget *w = m_graphicInstances.value(blockId))
            graphicInvoke(QStringLiteral("setFlowEnabled"),
                          QVariantList{ QVariant::fromValue(w), enabled });
    }
}

void MainWindow::onModuleOpened(const QString &moduleId, const QString &instanceId)
{
    if (moduleId == "trace") {
        QWidget *tab = m_traceInstances.value(instanceId);
        if (tab) {
            // 跳转到已有 Trace 实例
            const auto allTabs = m_editorArea->allTabWidgets();
            for (auto *tw : allTabs) {
                int idx = tw->indexOf(tab);
                if (idx >= 0) {
                    tw->setCurrentIndex(idx);
                    m_tabLabel->setText(tw->tabText(idx));
                    break;
                }
            }
        } else {
            // 新建 Trace 实例（instanceId 为空时自动生成编号，非空时使用给定
            // ID；编号解析/flow 注册/destroyed 清理在 createTraceInstance 内，
            // 拆分方案 B5）
            QString id = instanceId;
            if (id.isEmpty())
                id = QString("trace%1").arg(++m_traceCount);
            createTraceInstance(id);
        }
    } else if (moduleId == "graphic") {
        QWidget *gv = m_graphicInstances.value(instanceId);
        if (gv) {
            // 跳转到已有 Graphic 实例
            const auto allTabs = m_editorArea->allTabWidgets();
            for (auto *tw : allTabs) {
                int idx = tw->indexOf(gv);
                if (idx >= 0) {
                    tw->setCurrentIndex(idx);
                    m_tabLabel->setText(tw->tabText(idx));
                    break;
                }
            }
        } else {
            // 新建 Graphic 实例（同上，经 graphic 模块创建，拆分方案 B5）
            QString id = instanceId;
            if (id.isEmpty())
                id = QString("graphic%1").arg(++m_graphicCount);
            createGraphicInstance(id);
        }
    } else if (moduleId == "record") {
        onOpenRecordTab();
    } else if (moduleId == "watcher") {
        // Watcher 观测页（doc/Watcher 方案.md 方案 A：壳侧自持，直接开页）
        onOpenWatcher();
    } else if (moduleId == "filter") {
        // Filter 过滤块：双击打开过滤配置对话框（由 Flow 视图触发）
        onMeasurementViewFilterRequested();
    } else if (moduleId == "database" || moduleId == "can_parser") {
        // CAN parser 块：双击打开 DBC 选择对话框（由 Flow 视图触发）
        onMeasurementViewDbcSelectRequested();
    }
}

void MainWindow::onModuleInstanceClosed(const QString &moduleId, const QString &instanceId)
{
    // 使用 QTimer::singleShot(0) 延迟到下一轮事件循环，避免在右键菜单 exec() 的
    // 本地事件循环中触发 deleteLater() → destroyed → removeModuleInstance → rebuildScene()
    // 导致场景重建在 mousePressEvent 调用栈中执行而崩溃
    QTimer::singleShot(0, this, [this, moduleId, instanceId]() {
        QWidget *target = nullptr;
        if (moduleId == "trace")
            target = m_traceInstances.value(instanceId);
        else if (moduleId == "graphic")
            target = m_graphicInstances.value(instanceId);
        if (target) {
            const auto allTabs = m_editorArea->allTabWidgets();
            for (auto *tw : allTabs) {
                int idx = tw->indexOf(target);
                if (idx >= 0) {
                    m_editorArea->closeTab(tw, idx);  // 同步关闭标签页 + 刷新侧边栏
                    break;
                }
            }
        }
    });
}

void MainWindow::onMeasurementViewFilterRequested()
{
    // Flow 页面 Filter 块双击 → 打开过滤配置对话框
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        QObject *obj = mod->query("getActiveView").value<QObject *>();
        if (auto *view = qobject_cast<MeasurementSetupView *>(obj)) {
            view->showFilterConfigDialog();
        }
    }
}

void MainWindow::onMeasurementViewDbcSelectRequested()
{
    // Flow 页面 CAN parser 块双击 → 打开 DBC 选择对话框
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        QObject *obj = mod->query("getActiveView").value<QObject *>();
        if (auto *view = qobject_cast<MeasurementSetupView *>(obj)) {
            view->showDbcSelectDialog();
        }
    }
}

void MainWindow::unloadDbcFile(const QString &fileName)
{
    // 查找 DBC 文件完整路径
    QString filePath;
    for (const auto &f : m_dbcManager->files()) {
        if (f.fileName == fileName || f.filePath.endsWith(fileName)) {
            filePath = f.filePath;
            break;
        }
    }
    if (filePath.isEmpty()) return;

    // 关闭关联的 DBC 详情标签页
    if (m_editorArea) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains(fileName))
                    tw->removeTab(i);
            }
        }
    }
    m_dbcManager->unloadDbc(filePath);
}

