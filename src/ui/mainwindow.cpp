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
//  MainWindow 壳核心（B6 瘦身后，doc/拆分应用实施方案.md §4.5）
//  构造编排 + 模块编排（market/transceive/trace/graphic/flow 的
//  invoke/query 转发 + 实例表 createXxxInstance）+ makeShellContext。
//  其余职责见 mainwindow_setup/_chrome/_actions/_frameflow/_pages/
//  _project/_dialogs.cpp。
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("openbus - CAN/CAN FD 报文分析工具");
    resize(1400, 900);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    // ---- 数据层 / 核心服务 / 插件 / 驱动（mainwindow_setup.cpp 阶段 1）----
    setupCoreServices();

    // ---- UI 构建 ----
    createMenuBar();
    createWindowButtons();
    createStatusBar();  // 先创建状态栏（openTab 需要 m_tabLabel）
    createLayout();

    menuBar()->installEventFilter(this);

    // ---- 信号连接（mainwindow_setup.cpp 阶段 2~5；原构造函数直排代码
    //      按阶段拆出，调用顺序即原执行顺序）----
    connectExtensionsPanel();
    connectDataPipeline();
    connectSidePanels();

    m_bottomPanel->appendOutput("openbus 启动完成");
    updateActions();
    refreshPanelLists();

    connectProjectPanel();
}

MainWindow::~MainWindow()
{
    // DEF-11：析构体最先执行、派生类成员仍有效——在此断开实例的
    // destroyed 回调。基类 deleteChildren 阶段成员已析构完毕，回调里
    // m_traceInstances/m_graphicInstances.remove(...) 会访问已释放的
    // QMap 节点（UAF：退出阶段 SegFault，堆中毒 0xFEEEFEEE）。
    // 标签页运行期关闭时 widget 先于窗口析构，连接随对象消亡，不受影响。
    if (m_marketWidget)
        disconnect(m_marketWidget, &QObject::destroyed, this, nullptr);
    if (m_settingsPage)
        disconnect(m_settingsPage, &QObject::destroyed, this, nullptr);
    if (m_shortcutsPage)
        disconnect(m_shortcutsPage, &QObject::destroyed, this, nullptr);
    for (QWidget *w : m_traceInstances.values())
        disconnect(w, &QObject::destroyed, this, nullptr);
    for (QWidget *w : m_graphicInstances.values())
        disconnect(w, &QObject::destroyed, this, nullptr);
}

// ============================================================
//  侧边栏入口
// ============================================================

// setupTraceTab 已随 Trace 页迁入 TraceModule::createPage（拆分方案 B5：
// 过滤接线/信号联动/文件加载反馈在模块内完成，跨模块编排经 shellInvoke
// 回调壳槽 — frameDoubleClicked/frameAddToGraphic/traceSelectionChanged/
// traceFileLoaded，见 makeShellContext）

void MainWindow::openTab(QWidget *widget, const QString &label)
{
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        // 检查是否已存在同名标签
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == label) {
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(label);
                return;
            }
        }
    }
    int idx = m_editorArea->addTab(widget, label);
    tabs = m_editorArea->activeTabWidget();
    if (tabs)
        tabs->setCurrentIndex(idx);
    if (m_tabLabel)
        m_tabLabel->setText(label);
}

void MainWindow::setupMarketTab()
{
    // 经模块接口创建（拆分方案 B0：单体链接验证接口；B1 起 market 迁入
    // openbus_market.dll，壳不再 include markettab.h；插件操作请求的
    // PluginManager 连接在模块内完成）
    IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("market"));
    if (!mod)
        return;
    ShellContext ctx = makeShellContext();
    m_marketWidget = mod->createWidget(ctx);
    // Keep off-screen until openTab() reparents into the editor area
    if (m_marketWidget)
        m_marketWidget->hide();

    // 标签页被关闭后 widget 被删除 → 置空指针，避免悬空引用
    connect(m_marketWidget, &QObject::destroyed, this, [this]() {
        m_marketWidget = nullptr;
    });
}

void MainWindow::marketInvoke(const QString &action, const QVariant &arg)
{
    // 动作字符串约定见 core/module/imodule.h（refreshInstalled/focusSearch/
    // revealItem/installLocalFile）；页面不存在时静默忽略
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("market")))
        mod->invoke(action, arg);
}

// ============================================================
//  transceive 模块支持（拆分方案 B2）
// ============================================================

ShellContext MainWindow::makeShellContext()
{
    ShellContext ctx;
    ctx.mainWindow = this;
    ctx.player = m_player;
    ctx.recorder = m_recorder;
    ctx.deviceManager = m_deviceManager;
    ctx.simulator = m_simulator;
    ctx.dbcManager = m_dbcManager;
    ctx.appendOutput = [this](const QString &text) {
        m_bottomPanel->appendOutput(text);
    };
    ctx.addProblem = [this](int level, const QString &source, const QString &message) {
        m_bottomPanel->addProblem(level, source, message);
    };
    ctx.shellInvoke = [this](const QString &action, const QVariant &arg) {
        // 动作字符串约定见 core/module/imodule.h
        if (action == QStringLiteral("play")) {
            onPlay();
        } else if (action == QStringLiteral("pause")) {
            onPause();
        } else if (action == QStringLiteral("stop")) {
            onStop();
        } else if (action == QStringLiteral("setSpeed")) {
            onSpeedChanged(arg.toDouble());
        } else if (action == QStringLiteral("setAutoScroll")) {
            onAutoScrollToggled(arg.toBool());
        } else if (action == QStringLiteral("clearTraceGraphic")) {
            // 清除所有 Trace 和 Graphic 视图（加载新文件时的壳编排；经模块，B5）
            graphicInvoke(QStringLiteral("clearDataAll"));
            traceInvoke(QStringLiteral("clearTraceAll"));
        } else if (action == QStringLiteral("updateActions")) {
            updateActions();
        } else if (action == QStringLiteral("statusMessage")) {
            m_statusLabel->setText(arg.toString());
        } else if (action == QStringLiteral("connMessage")) {
            // 设备连接状态栏（B4：设备页经 flow 模块）
            m_connLabel->setText(arg.toString());
        } else if (action == QStringLiteral("deviceDisconnected")) {
            m_connLabel->setText(QStringLiteral("Disconnected"));
            m_measurementRunning = false;
            traceInvoke(QStringLiteral("setRunningAll"), false);
            m_bottomPanel->appendOutput(QStringLiteral("Data stream stopped"));
        } else if (action == QStringLiteral("openOfflineAnalysis")) {
            onOpenOfflineAnalysisTab();
        } else if (action == QStringLiteral("openDevicePage")) {
            openDevicePage();
        } else if (action == QStringLiteral("sendPageOpened")) {
            onOpenSendTab();
        } else if (action == QStringLiteral("measurementToggled")) {
            onMeasurementToggled(arg.toBool());
        } else if (action == QStringLiteral("moduleToggled")) {
            const QVariantList l = arg.toList();
            if (l.size() == 3)
                onModuleToggled(l.at(0).toString(), l.at(1).toString(), l.at(2).toBool());
        } else if (action == QStringLiteral("moduleOpened")) {
            const QVariantList l = arg.toList();
            if (l.size() == 2)
                onModuleOpened(l.at(0).toString(), l.at(1).toString());
        } else if (action == QStringLiteral("moduleInstanceClosed")) {
            const QVariantList l = arg.toList();
            if (l.size() == 2)
                onModuleInstanceClosed(l.at(0).toString(), l.at(1).toString());
        } else if (action == QStringLiteral("dbcRemoveRequested")) {
            unloadDbcFile(arg.toString());
        } else if (action == QStringLiteral("signalDoubleClicked")
                   || action == QStringLiteral("signalAddToTrace")) {
            // DBC 详情页信号联动（拆分方案 B3）：双击/加 Graphic → 信号→Graphic 编排；
            // 加 Trace → 信号→Trace 编排（两槽签名一致，统一解包转发）
            const QVariantList l = arg.toList();
            if (l.size() == 2) {
                if (action == QStringLiteral("signalDoubleClicked"))
                    onSignalDoubleClicked(l.at(0).toUInt(), l.at(1).toString());
                else
                    onSignalAddToTrace(l.at(0).toUInt(), l.at(1).toString());
            }
        } else if (action == QStringLiteral("frameDoubleClicked")) {
            // Trace 页双击帧（拆分方案 B5：TraceModule 回调）→ 壳的
            // 帧→过滤 + 帧信号→Graphic 编排
            onFrameDoubleClicked(arg.value<CanFrame>());
        } else if (action == QStringLiteral("frameAddToGraphic")) {
            // Trace 右键"添加信号到 Graphic"（拆分方案 B5）→ 壳查 DBC 加全部信号
            onFrameAddToGraphic(arg.value<CanFrame>());
        } else if (action == QStringLiteral("traceSelectionChanged")) {
            // Trace selection count → status bar (kept for older callers)
            const int n = arg.toInt();
            m_selectedLabel->setText(n > 0 ? QStringLiteral("Sel: %1").arg(n)
                                           : QStringLiteral("Sel: 0"));
        } else if (action == QStringLiteral("traceStatus")) {
            // Concise Trace status from TraceTab (replaces in-tab status strip)
            const QVariantMap info = arg.toMap();
            const int captured = info.value(QStringLiteral("captured")).toInt();
            const int displayed = info.value(QStringLiteral("displayed")).toInt();
            const int selected = info.value(QStringLiteral("selected")).toInt();
            const QString selDetail = info.value(QStringLiteral("selDetail")).toString();
            if (displayed == captured || captured <= 0)
                m_frameCountLabel->setText(QStringLiteral("%1 frames").arg(displayed));
            else
                m_frameCountLabel->setText(
                    QStringLiteral("%1 / %2").arg(displayed).arg(captured));
            if (selected <= 0)
                m_selectedLabel->setText(QStringLiteral("Sel: 0"));
            else if (!selDetail.isEmpty() && selected == 1)
                m_selectedLabel->setText(QStringLiteral("Sel: 1 @ %1").arg(selDetail));
            else
                m_selectedLabel->setText(QStringLiteral("Sel: %1").arg(selected));
        } else if (action == QStringLiteral("traceFileLoaded")) {
            // Trace file drop load complete → output + status frames
            const int count = arg.toInt();
            if (count < 0) {
                m_bottomPanel->appendOutput(QStringLiteral("File load failed"));
            } else {
                m_bottomPanel->appendOutput(
                    QStringLiteral("Loaded %1 frames").arg(count));
                m_frameCountLabel->setText(
                    QStringLiteral("%1 frames").arg(count));
            }
        } else if (action == QStringLiteral("ingestTxEcho")) {
            // Unified Tx loopback (plugin / Transceive) → CaptureLog + SampleStore + Flow.
            // Same hub as device Rx; measurement gate applies via onFramesReceived.
            QVector<CanFrame> frames;
            if (arg.canConvert<QVector<CanFrame>>()) {
                frames = arg.value<QVector<CanFrame>>();
            } else if (arg.canConvert(QVariant::List)) {
                for (const QVariant &v : arg.toList()) {
                    if (v.canConvert<CanFrame>())
                        frames.append(v.value<CanFrame>());
                }
            }
            if (!frames.isEmpty())
                onFramesReceived(frames);
        } else if (action == QStringLiteral("appendFrames")) {
            // Offline import / local Trace ring only — NOT the live bus path.
            // Live Tx must use ingestTxEcho (CaptureLog camera SoT).
            if (arg.canConvert(QVariant::List)) {
                auto l = arg.toList();
                if (l.size() >= 2) {
                    traceInvoke(QStringLiteral("appendFrames"), arg);
                    if (m_autoScroll && l.size() > 1) {
                        auto frameList = l[1].toList();
                        if (!frameList.isEmpty())
                            traceInvoke(QStringLiteral("setAutoScroll"), true);
                    }
                }
            }
        }
    };
    return ctx;
}

void MainWindow::transceiveInvoke(const QString &action, const QVariant &arg)
{
    // 动作字符串约定见 core/module/imodule.h（setRecording/setFileInfo/
    // setProgress）；页面不存在时模块内静默忽略
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("transceive")))
        mod->invoke(action, arg);
}

QVariant MainWindow::transceiveQuery(const QString &what, const QVariant &arg)
{
    // 查询约定见 core/module/imodule.h（offlineFiles → QStringList）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("transceive")))
        return mod->query(what, arg);
    return {};
}

// ============================================================
//  trace / graphic 模块支持（拆分方案 B5）
// ============================================================

void MainWindow::traceInvoke(const QString &action, const QVariant &arg)
{
    // 动作字符串约定见 core/module/imodule.h（onFrame/appendFrames/
    // setAutoScroll/setRunning/setRunningAll/clearTraceAll/clearAll/
    // setFilterExpression/jumpToFrame/editColorRules）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("trace")))
        mod->invoke(action, arg);
}

QVariant MainWindow::traceQuery(const QString &what, const QVariant &arg)
{
    // 查询约定见 core/module/imodule.h（isTrace/instance/filterExpression/
    // frameCount/activeInstance/colorRules）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("trace")))
        return mod->query(what, arg);
    return {};
}

void MainWindow::graphicInvoke(const QString &action, const QVariant &arg)
{
    // 动作字符串约定见 core/module/imodule.h（onFrame/setFlowEnabled/
    // clearDataAll/addSignal/addSignals/loadSignalConfigs）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("graphic")))
        mod->invoke(action, arg);
}

QVariant MainWindow::graphicQuery(const QString &what, const QVariant &arg)
{
    // 查询约定见 core/module/imodule.h（isGraphic/instance/lastInstance/
    // signalConfigs/dataWindow/activeInstance）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("graphic")))
        return mod->query(what, arg);
    return {};
}

QWidget *MainWindow::createTraceInstance(const QString &id)
{
    // 已存在则直接返回（applyProjectState/构造兜底可能重复调用）
    if (QWidget *existing = m_traceInstances.value(id))
        return existing;

    IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("trace"));
    if (!mod)
        return nullptr;
    ShellContext ctx = makeShellContext();
    QWidget *w = mod->createPage(QStringLiteral("trace"), id, ctx);
    if (!w)
        return nullptr;

    // 实例号同步（id 形如 trace3 时抬高计数，避免后续自动编号冲突）
    QRegularExpression re("trace(\\d+)", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(id);
    if (m.hasMatch()) {
        int n = m.captured(1).toInt();
        if (n > m_traceCount) m_traceCount = n;
    }
    QString numPart = id;
    numPart.remove("trace", Qt::CaseInsensitive);
    // 标题与侧栏模板「帧列表」一致（截图反馈 2026-08-23）
    const QString title = QString("帧列表%1").arg(numPart.toInt());

    openTab(w, title);
    m_traceInstances[id] = w;
    flowInvoke(QStringLiteral("addModuleInstance"),
               QVariantList{ QStringLiteral("trace"), id, title });
    connect(w, &QObject::destroyed, this, [this, id](QObject *) {
        m_traceInstances.remove(id);
        // 延迟到下一轮事件循环，避免在析构链中同步修改场景导致崩溃
        QMetaObject::invokeMethod(this, [this, id]() {
            flowInvoke(QStringLiteral("removeModuleInstance"),
                       QVariantList{ QStringLiteral("trace"), id });
        }, Qt::QueuedConnection);
    });
    return w;
}

QWidget *MainWindow::createGraphicInstance(const QString &id)
{
    if (QWidget *existing = m_graphicInstances.value(id))
        return existing;

    IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("graphic"));
    if (!mod)
        return nullptr;
    ShellContext ctx = makeShellContext();
    QWidget *w = mod->createPage(QStringLiteral("graphic"), id, ctx);
    if (!w)
        return nullptr;

    QRegularExpression re("graphic(\\d+)", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(id);
    if (m.hasMatch()) {
        int n = m.captured(1).toInt();
        if (n > m_graphicCount) m_graphicCount = n;
    }
    QString numPart = id;
    numPart.remove("graphic", Qt::CaseInsensitive);
    // 标题与侧栏模板「时序波形」一致（截图反馈 2026-08-23）
    const QString title = QString("时序波形%1").arg(numPart.toInt());

    openTab(w, title);
    m_graphicInstances[id] = w;
    flowInvoke(QStringLiteral("addModuleInstance"),
               QVariantList{ QStringLiteral("graphic"), id, title });
    connect(w, &QObject::destroyed, this, [this, id](QObject *) {
        m_graphicInstances.remove(id);
        QMetaObject::invokeMethod(this, [this, id]() {
            flowInvoke(QStringLiteral("removeModuleInstance"),
                       QVariantList{ QStringLiteral("graphic"), id });
        }, Qt::QueuedConnection);
    });
    return w;
}

QWidget *MainWindow::resolveGraphicTarget()
{
    // 当前页是 Graphic 则用之；否则取活跃标签组中最后一个 Graphic 标签页；
    // 都没有则经模块新建（原 setupTraceTab/onFrameDoubleClicked 内联逻辑）
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        QWidget *cur = tabs->currentWidget();
        if (graphicQuery(QStringLiteral("isGraphic"), QVariant::fromValue(cur)).toBool())
            return cur;
        for (int i = tabs->count() - 1; i >= 0; --i) {
            QWidget *w = tabs->widget(i);
            if (graphicQuery(QStringLiteral("isGraphic"), QVariant::fromValue(w)).toBool())
                return w;
        }
    }
    return createGraphicInstance(QString("graphic%1").arg(++m_graphicCount));
}

QVariantMap MainWindow::buildSignalMap(quint32 canId, bool extended, const QString &name,
                                       const DbcSignal &sig)
{
    // GraphicView::Signal 的跨模块序列化契约（graphicmodule.cpp 用
    // dbcSignalFromMap 反序列化；共享实现在 core/dbcdata.h）
    QVariantMap m;
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("canId"), canId);
    m.insert(QStringLiteral("extended"), extended);
    m.insert(QStringLiteral("dbcSig"), dbcSignalToMap(sig));
    return m;
}


void MainWindow::flowInvoke(const QString &action, const QVariant &arg)
{
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        // signal_generator：打开信号发送 Tab
        if (action == QStringLiteral("sendPageOpened")) {
            onOpenSendTab();
            return;
        }
        mod->invoke(action, arg);
    }
}

QVariant MainWindow::flowQuery(const QString &what, const QVariant &arg)
{
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow")))
        return mod->query(what, arg);
    return {};
}

