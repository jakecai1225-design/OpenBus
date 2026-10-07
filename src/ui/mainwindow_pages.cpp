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
#include "ui/watcherview.h"    // Watcher 观测页（doc/Watcher方案.md 方案 A：壳侧页面）
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
#include "ui/welcomepage.h"
#include "core/file_import/file_importer.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"
#include "core/sessionmanager.h"
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
#include <QInputDialog>
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
//  MainWindow 页面打开槽（B6 拆分自 mainwindow.cpp）
//  侧边栏入口 → 各业务页（经 ModuleRegistry 创建）+ 杂项页面 +
//  面板列表刷新
// ============================================================

void MainWindow::onOpenMarketTab()
{
    // 标签页可能已被关闭并删除，需要重建
    if (!m_marketWidget)
        setupMarketTab();
    if (!m_marketWidget)
        return;
    setPageKind(m_marketWidget, QStringLiteral("extensions"));
    openTab(m_marketWidget, tr("Extensions"));
    marketInvoke(QStringLiteral("refreshInstalled"));
    // ＋新增设备跳转后直接聚焦搜索（方案 §13.6）
    marketInvoke(QStringLiteral("focusSearch"));
}

void MainWindow::onOpenWelcomeTab()
{
    if (!m_welcomePage) {
        m_welcomePage = new WelcomePage(this);
        connect(m_welcomePage, &QObject::destroyed, this, [this]() {
            m_welcomePage = nullptr;
        });
        connect(m_welcomePage, &WelcomePage::newProjectRequested, this, [this]() {
            bool ok = false;
            const QString name = QInputDialog::getText(
                this, QStringLiteral("New Project"),
                QStringLiteral("Project name:"), QLineEdit::Normal,
                QStringLiteral("Untitled"), &ok);
            if (ok && !name.trimmed().isEmpty())
                onProjectCreated(name.trimmed());
        });
        connect(m_welcomePage, &WelcomePage::openProjectRequested,
                this, &MainWindow::onOpenProject);
        connect(m_welcomePage, &WelcomePage::openRecentRequested, this,
                [this](const QString &path) {
            if (path.isEmpty() || !QFile::exists(path)) {
                m_bottomPanel->appendOutput(
                    QStringLiteral("Recent project not found: ") + path);
                if (m_welcomePage)
                    m_welcomePage->refreshRecent();
                return;
            }
            if (ProjectManager::instance()->loadProject(path)) {
                applyProjectState();
                const QString loadedName =
                    ProjectManager::instance()->currentProjectName();
                m_sideBar->projectPanel()->activateProject(path, loadedName);
                m_bottomPanel->appendOutput(
                    QStringLiteral("Project loaded: ") + loadedName);
            }
        });
        connect(m_welcomePage, &WelcomePage::openDeviceRequested,
                this, &MainWindow::openDevicePage);
        connect(m_welcomePage, &WelcomePage::openFlowRequested,
                this, &MainWindow::onOpenMeasurementSetup);
        connect(m_welcomePage, &WelcomePage::openMarketRequested,
                this, &MainWindow::onOpenMarketTab);
        connect(m_welcomePage, &WelcomePage::openTraceRequested,
                this, &MainWindow::onOpenTraceTab);
        connect(m_welcomePage, &WelcomePage::openGraphicRequested,
                this, &MainWindow::onNewGraphicRequested);
        connect(m_welcomePage, &WelcomePage::openDbcPanelRequested, this, [this]() {
            m_activityBar->setCurrentActivity(ActivityBar::Dbc);
            m_sideBar->showPanel(static_cast<int>(ActivityBar::Dbc));
        });
        connect(m_welcomePage, &WelcomePage::openShortcutsRequested, this, [this]() {
            onSettingsRequested(QStringLiteral("shortcuts"));
        });
        connect(m_welcomePage, &WelcomePage::openAboutRequested,
                this, &MainWindow::showAboutDialog);
        connect(m_welcomePage, &WelcomePage::openReleaseNotesRequested,
                this, &MainWindow::showReleaseNotes);
        connect(m_welcomePage, &WelcomePage::openDocsRequested, this, []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("http://sin.org.cn/docs")));
        });
        connect(m_welcomePage, &WelcomePage::clearRecentRequested, this, [this]() {
            SessionManager::instance()->clearRecent();
            if (m_welcomePage)
                m_welcomePage->refreshRecent();
        });
    } else {
        m_welcomePage->refreshRecent();
    }
    setPageKind(m_welcomePage, QStringLiteral("welcome"));
    openTab(m_welcomePage, tr("Welcome"));
}

void MainWindow::onOpenTraceTab()
{
    // 经 trace 模块创建（拆分方案 B5）；注册为实例以纳入 Flow 门控与帧分发
    createTraceInstance(QString("trace%1").arg(++m_traceCount));
}

void MainWindow::onTracePageSelected(int row)
{
    const auto allTabs = m_editorArea->allTabWidgets();
    int traceIdx = 0;
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (isTraceTabText(tw->tabText(i))) {
                if (traceIdx == row) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    return;
                }
                traceIdx++;
            }
        }
    }
}

void MainWindow::onOpenSendTab()
{
    if (activateTabByPageKind(QStringLiteral("send")))
        return;
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("transceive"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("signalsend"), ctx)) {
            setPageKind(page, QStringLiteral("send"));
            openTab(page, tr("Send"));
        }
    }
}

void MainWindow::onOpenPlaybackTab()
{
    if (activateTabByPageKind(QStringLiteral("playback")))
        return;
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("transceive"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("playback"), ctx)) {
            setPageKind(page, QStringLiteral("playback"));
            openTab(page, tr("Playback"));
        }
    }
}

void MainWindow::onOpenOfflineAnalysisTab()
{
    if (activateTabByPageKind(QStringLiteral("offline")))
        return;
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("transceive"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("offlineanalysis"), ctx)) {
            setPageKind(page, QStringLiteral("offline"));
            openTab(page, tr("Offline Analysis"));
        }
    }
}

void MainWindow::onOpenRecordTab()
{
    if (activateTabByPageKind(QStringLiteral("record")))
        return;
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("transceive"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("record"), ctx)) {
            setPageKind(page, QStringLiteral("record"));
            openTab(page, tr("Record"));
        }
    }
}

// setupSendTab/setupPlaybackTab/setupOfflineAnalysisTab/setupRecordTab 已随
// 收发四页迁入 TransceiveModule（拆分方案 B2 §4.5：模块自己连接自己的信号槽）

void MainWindow::onOpenDeviceTab(int deviceKind, int devIndex, const QString &deviceName, int deviceType)
{
    if (activateTabByPageKind(QStringLiteral("device"))) {
        flowInvoke(QStringLiteral("setDevice"),
                   QVariantList{ deviceKind, devIndex, deviceName, deviceType });
        return;
    }

    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(
                QStringLiteral("device"),
                QVariantList{ deviceKind, devIndex, deviceName, deviceType }, ctx)) {
            setPageKind(page, QStringLiteral("device"));
            openTab(page, tr("Devices"));
        }
    }
}

void MainWindow::openDevicePage()
{
    if (activateTabByPageKind(QStringLiteral("device")))
        return;
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("device"), ctx)) {
            setPageKind(page, QStringLiteral("device"));
            openTab(page, tr("Devices"));
        }
    }
}

// setupDeviceTab 已随设备连接页迁入 FlowModule（拆分方案 B4：
// 数据层操作模块内完成，状态栏/实例门控经 shellInvoke 回调壳）

// linkGraphicCursor 已随 Graphic 页迁入 GraphicModule::createPage（拆分方案 B5：
// 视图间游标联动在模块内逐对互连，壳不再持有 GraphicView 类型）

void MainWindow::onNewGraphicRequested()
{
    // 经 graphic 模块创建（拆分方案 B5：装配/游标联动在模块内完成）
    createGraphicInstance(QString("graphic%1").arg(++m_graphicCount));
}

void MainWindow::onOpenMeasurementSetup()
{
    // 查找已有的 flow 标签页
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("Flow", Qt::CaseInsensitive)) {
                tw->setCurrentIndex(i);
                m_tabLabel->setText(tw->tabText(i));
                return;
            }
        }
    }

    // 创建新的 flow 标签页（拆分方案 B4：装配在 flow 模块内完成，
    // 数据层操作模块侧处理，跨模块编排经 shellInvoke 回调壳槽）
    QWidget *view = nullptr;
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("flow"))) {
        ShellContext ctx = makeShellContext();
        view = mod->createPage(QStringLiteral("setup"), ctx);
    }
    if (!view)
        return;
    // measurementToggled/moduleToggled/moduleOpened/moduleInstanceClosed 等
    // 编排连接已迁入 FlowModule → 经 shellInvoke 回调壳槽（拆分方案 B4）

    // moduleInstanceClosed/dbcSelectRequested/dbcRemoveRequested/filterRulesChanged
    // 连接已迁入 FlowModule（拆分方案 B4：前者经 shellInvoke 回调壳，后三者模块侧完成）

    setPageKind(view, QStringLiteral("flow"));
    openTab(view, tr("CAN Flow"));

    // 注册默认 Trace1/Graphic1 实例到 flow 画布（经模块创建或复用现有实例，
    // 拆分方案 B5；createXxxInstance 内部完成 openTab + flow 注册 + destroyed 清理）
    createTraceInstance(QStringLiteral("trace1"));
    createGraphicInstance(QStringLiteral("graphic1"));
}


// ============================================================
//  P0/P1 新增功能实现
// ============================================================

void MainWindow::onOpenDataWindow()
{
    // Data Window 随 Graphic 页迁入 openbus_graphic.dll（拆分方案 B5：
    // 单实例缓存在模块内，壳只负责开标签页）
    if (IBusinessModule *mod = ModuleRegistry::instance()->module(QStringLiteral("graphic"))) {
        ShellContext ctx = makeShellContext();
        if (QWidget *page = mod->createPage(QStringLiteral("datawindow"), ctx)) {
            setPageKind(page, QStringLiteral("datawindow"));
            openTab(page, tr("Data Window"));
        }
    }
}

void MainWindow::onOpenIOGraph()
{
    if (!m_ioGraph) {
        m_ioGraph = new IOGraphView(this);
        setPageKind(m_ioGraph, QStringLiteral("iograph"));
    }
    openTab(m_ioGraph, tr("I/O Graph"));
}

void MainWindow::onOpenWatcher()
{
    if (!m_watcherView) {
        m_watcherView = new WatcherView(m_dbcManager, m_busStats, this);
        setPageKind(m_watcherView, QStringLiteral("watcher"));
        connect(m_watcherView, &QObject::destroyed, this, [this]() {
            m_watcherView = nullptr;
        });
    }
    openTab(m_watcherView, tr("Watcher"));
}

void MainWindow::onOpenColorRuleEditor()
{
    // 着色规则编辑随 Trace 页迁入 TraceModule（拆分方案 B5：ColorRuleEditor
    // 归模块所有，规则加载/应用到全部实例在模块内完成）
    traceInvoke(QStringLiteral("editColorRules"));
}

void MainWindow::onBookmarkJumped(int frameIndex)
{
    // 跳转到指定帧（经 trace 模块，拆分方案 B5；当前页非 Trace 时忽略）
    QWidget *traceTab = m_editorArea->currentWidget();
    if (!traceQuery(QStringLiteral("isTrace"), QVariant::fromValue(traceTab)).toBool())
        return;
    traceInvoke(QStringLiteral("jumpToFrame"),
                QVariantList{ QVariant::fromValue(traceTab), frameIndex });
}

// onTriggerRecording 已随录制页迁入 TransceiveModule（拆分方案 B2；
// TriggerRecorder 归模块所有，状态栏提示经 ctx.shellInvoke("statusMessage")）

void MainWindow::onGraphicPageSelected(int row)
{
    const auto allTabs = m_editorArea->allTabWidgets();
    int graphicIdx = 0;
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (isGraphicTabText(tw->tabText(i))) {
                if (graphicIdx == row) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    return;
                }
                graphicIdx++;
            }
        }
    }
    // 如果没找到匹配的标签页，创建新的
    onNewGraphicRequested();
}

void MainWindow::onSettingsRequested(const QString &section)
{
    // Sidebar settings entries open as editor tabs (not modal dialogs).
    // Stable keys: "shortcuts" | "general" | category name (English).
    // Legacy Chinese labels still accepted for older sessions.
    const bool isShortcuts =
        section == QStringLiteral("shortcuts")
        || section == QStringLiteral("Keyboard Shortcuts")
        || section == QStringLiteral("快捷键");
    if (isShortcuts) {
        if (!m_shortcutsPage) {
            m_shortcutsPage = new ShortcutsPage(this);
            connect(m_shortcutsPage, &QObject::destroyed, this, [this]() {
                m_shortcutsPage = nullptr;
            });
        }
        openTab(m_shortcutsPage, tr("Keyboard Shortcuts"));
        return;
    }

    if (!m_settingsPage) {
        m_settingsPage = new SettingsPage(this);
        connect(m_settingsPage, &QObject::destroyed, this, [this]() {
            m_settingsPage = nullptr;
        });
    }
    openTab(m_settingsPage, tr("Settings"));

    QString category = section;
    if (category == QStringLiteral("general")
        || category == QStringLiteral("通用设置")
        || category == QStringLiteral("通用"))
        category = QStringLiteral("General");
    else if (category.endsWith(QStringLiteral("设置")))
        category.chop(2);
    m_settingsPage->setCategory(category);
}

void MainWindow::refreshPanelLists()
{
    QStringList traceNames, graphicNames, flowNames;
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            QString name = tw->tabText(i);
            if (isTraceTabText(name))
                traceNames << name;
            if (isGraphicTabText(name))
                graphicNames << name;
            if (name.contains(QStringLiteral("Flow"), Qt::CaseInsensitive))
                flowNames << name;
        }
    }
    m_sideBar->tracePanel()->refreshList(traceNames);
    m_sideBar->graphicConfigPanel()->refreshList(graphicNames);
    // Flow 面板「已打开」区（VS Code 版式：已打开在上，F1 多实例前 = 单画布行）
    m_sideBar->analysisPanel()->refreshOpenList(flowNames);
}

