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
#include "ui/traceview.h"
#include "ui/graphicview.h"
#include "ui/filterbar.h"
#include "ui/activitybar.h"
#include "ui/panels/sidebarpanels.h"
#include "ui/thememanager.h"
#include "ui/bottompanel.h"
#include "ui/rightpanel.h"
#include "ui/spliteditorarea.h"
#include "ui/signalsendtab.h"
#include "ui/playbacktab.h"
#include "ui/offlineanalysistab.h"
#include "ui/recordtab.h"
#include "ui/dbcdetailtab.h"
#include "ui/measurementsetupview.h"
#include "ui/deviceconnectiontab.h"
#include "core/driver/driverregistry.h"
#include "ui/markettab.h"
#include "ui/plugindetailpage.h"
// ui/udsview.h, ui/canopenview.h 已移除 — UDS/CANopen 由插件 uds-diagnostic/canopen-explorer 提供
#include "ui/tools/dbcsignallistview.h"
#include "ui/datawindow.h"
#include "ui/tools/iographview.h"
#include "ui/colorruleeditor.h"
#include "core/busstatistics.h"
#include "core/filterpresetmanager.h"
#include "core/bookmarkmanager.h"
#include "core/triggerrecorder.h"
#include "utils/canutils.h"
#include "core/appconfig.h"
#include "core/projectmanager.h"
#include "ui/settingsdialog.h"
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("openbus - CAN/CAN FD 报文分析工具");
    resize(1400, 900);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    // ---- 数据层 ----
    m_dbcManager = new DbcManager(this);
    m_recorder  = new Recorder(this);
    m_player    = new Player(this);
    m_simulator = new CanSimulator(this);
    m_deviceManager = new CanDeviceManager(this);

    // ---- P0/P1 核心服务 ----
    m_busStats = new BusStatistics(this);
    m_filterPresets = new FilterPresetManager(this);
    m_bookmarkMgr = new BookmarkManager(this);

    // ---- 插件系统 ----
    m_pluginManager = PluginManager::instance();
    connect(m_pluginManager, &PluginManager::outputMessage,
            this, &MainWindow::onPluginOutput);
    connect(m_pluginManager, &PluginManager::commandRegistered,
            this, &MainWindow::onPluginCommandRegistered);
    connect(m_pluginManager, &PluginManager::sendFrameRequested,
            this, &MainWindow::onPluginSendFrame);
    connect(m_pluginManager, &PluginManager::requestSelectedFrames,
            this, &MainWindow::onPluginRequestSelectedFrames);
    m_pluginManager->initialize();

    // ---- 驱动系统 ----
    // 清理待卸载目录 + 扫描加载外置 .odp 驱动（须在 UI 构建前，设备树首建即完整）
    DriverRegistry::instance()->initialize();

    // ---- UI 构建 ----
    createMenuBar();
    createWindowButtons();
    createStatusBar();  // 先创建状态栏（openTab 需要 m_tabLabel）
    createLayout();

    menuBar()->installEventFilter(this);

    // ---- 信号连接 ----

    // ActivityBar
    connect(m_activityBar, &ActivityBar::activityChanged,
            this, &MainWindow::onActivityChanged);
    connect(m_activityBar, &ActivityBar::activityToggled,
            this, &MainWindow::onActivityToggled);

    // ExtensionsPanel → 插件操作
    auto *extPanel = m_sideBar->extensionsPanel();
    if (extPanel) {
        connect(extPanel, &ExtensionsPanel::commandTriggered,
                this, [this](const QString &id) {
            if (m_pluginManager) m_pluginManager->executeCommand(id);
        });
        connect(extPanel, &ExtensionsPanel::pluginToggleRequested,
                this, [this](const QString &name, bool enable) {
            if (m_pluginManager) m_pluginManager->setPluginEnabled(name, enable);
        });
        connect(extPanel, &ExtensionsPanel::pluginActivated,
                this, [this](const QString &name) {
            if (m_pluginManager) m_pluginManager->reactivatePlugin(name);
        });
        connect(extPanel, &ExtensionsPanel::pluginDeactivateRequested,
                this, [this](const QString &name) {
            if (m_pluginManager) {
                m_pluginManager->deactivatePlugin(name);
                emit m_pluginManager->pluginListChanged();
            }
        });
        connect(extPanel, &ExtensionsPanel::pluginUninstallRequested,
                this, [this](const QString &name) {
            if (QMessageBox::question(this, QStringLiteral("卸载插件"),
                                      QStringLiteral("确定卸载插件 %1？").arg(name))
                != QMessageBox::Yes)
                return;
            const QString err = m_pluginManager
                                    ? m_pluginManager->uninstallPlugin(name)
                                    : QStringLiteral("插件系统未初始化");
            if (!err.isEmpty())
                QMessageBox::warning(this, QStringLiteral("卸载插件"), err);
        });
        connect(extPanel, &ExtensionsPanel::pluginSelected,
                this, &MainWindow::openPluginDetail);
        connect(extPanel, &ExtensionsPanel::installOpkRequested,
                this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择插件包"), QString(),
                QStringLiteral("openbus 插件包 (*.opk);;所有文件 (*)"));
            if (path.isEmpty())
                return;
            const QString err = m_pluginManager
                                    ? m_pluginManager->installPackage(path)
                                    : QStringLiteral("插件系统未初始化");
            if (!err.isEmpty())
                QMessageBox::warning(this, QStringLiteral("安装插件"), err);
            else
                QMessageBox::information(this, QStringLiteral("安装插件"),
                                         QStringLiteral("插件安装成功"));
        });
        connect(extPanel, &ExtensionsPanel::refreshPluginsRequested,
                this, [this]() {
            refreshPluginList();
            if (m_marketTab) m_marketTab->refreshInstalled();
        });
        connect(m_pluginManager, &PluginManager::pluginListChanged,
                this, &MainWindow::refreshPluginList);
    }

    // MarketTab → 统一插件市场标签页（见 setupMarketTab）
    setupMarketTab();

    // pluginListChanged → 刷新市场页已装列表（连接在 manager 上，标签页删除后仍安全）
    if (m_pluginManager) {
        connect(m_pluginManager, &PluginManager::pluginListChanged,
                this, [this]() {
            if (m_marketTab) m_marketTab->refreshInstalled();
        });
    }

    // 模拟器 → 帧接收
    connect(m_simulator, &CanSimulator::frameGenerated,
            this, &MainWindow::onFrameReceived);

    // 硬件设备管理器 → 帧接收（与模拟器同信号）
    connect(m_deviceManager, &CanDeviceManager::frameGenerated,
            this, &MainWindow::onFrameReceived);
    connect(m_deviceManager, &CanDeviceManager::connectionChanged,
            this, [this](bool connected, const QString &name) {
        if (connected) {
            m_connLabel->setText(QStringLiteral("已连接: %1").arg(name));
            m_bottomPanel->appendOutput(QStringLiteral("硬件已连接: %1").arg(name));
        } else {
            m_connLabel->setText("未连接");
            m_bottomPanel->appendOutput(QStringLiteral("硬件已断开"));
        }
    });
    connect(m_deviceManager, &CanDeviceManager::errorOccurred,
            this, [this](const QString &msg) {
        m_bottomPanel->appendOutput(QStringLiteral("%1").arg(msg));
    });

    // 回放器
    connect(m_player, &Player::framePlayed, this, &MainWindow::onFramePlayed);
    connect(m_player, &Player::progressChanged, this, &MainWindow::onPlayerProgress);
    connect(m_player, &Player::stateChanged, this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &Player::finished, this, &MainWindow::onPlayerFinished);

    // 录制器
    connect(m_recorder, &Recorder::recordingStarted, this, [this](const QString &) {
        m_recording = true;
        m_bottomPanel->appendOutput("录制开始");
        if (m_recordTab) m_recordTab->setRecording(true);
        updateActions();
    });
    connect(m_recorder, &Recorder::recordingStopped, this, [this](const QString &path, int count) {
        m_recording = false;
        m_bottomPanel->appendOutput(QString("录制结束: %1 (%2 帧)").arg(path).arg(count));
        if (m_recordTab) m_recordTab->setRecording(false);
        updateActions();
    });

    // 侧边栏面板
    connect(m_sideBar->dbcPanel(), &DbcPanel::dbcFileClicked,
            this, &MainWindow::onDbcFileClicked);
    connect(m_sideBar->dbcPanel(), &DbcPanel::dbcRemoveRequested,
            this, [this](const QString &filePath) {
        // 关闭关联的 DBC 详情标签页
        QString fileName = QFileInfo(filePath).fileName();
        if (m_editorArea) {
            const auto allTabs = m_editorArea->allTabWidgets();
            for (auto *tw : allTabs) {
                for (int i = tw->count() - 1; i >= 0; --i) {
                    if (tw->tabText(i).contains(fileName))
                        tw->removeTab(i);
                }
            }
        }
    });
    connect(m_sideBar->tracePanel(), &TracePanel::openTraceRequested,
            this, &MainWindow::onOpenTraceTab);
    connect(m_sideBar->tracePanel(), &TracePanel::tracePageSelected,
            this, &MainWindow::onTracePageSelected);
    connect(m_sideBar->tracePanel(), &TracePanel::traceDeleteRequested,
            this, [this](int row) {
        // 侧边栏删除 Trace → 找到对应标签页并关闭（触发完整清理链）
        const auto allTabs = m_editorArea->allTabWidgets();
        int traceIdx = 0;
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                if (tw->tabText(i).contains("Trace")) {
                    if (traceIdx == row) {
                        m_editorArea->closeTab(tw, i);
                        return;
                    }
                    traceIdx++;
                }
            }
        }
    });
    connect(m_sideBar->transceivePanel(), &TransceivePanel::openSendRequested,
            this, &MainWindow::onOpenSendTab);
    connect(m_sideBar->transceivePanel(), &TransceivePanel::openPlaybackRequested,
            this, &MainWindow::onOpenPlaybackTab);
    connect(m_sideBar->transceivePanel(), &TransceivePanel::openOfflineAnalysisRequested,
            this, &MainWindow::onOpenOfflineAnalysisTab);
    connect(m_sideBar->transceivePanel(), &TransceivePanel::openRecordRequested,
            this, &MainWindow::onOpenRecordTab);
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::newGraphicRequested,
            this, &MainWindow::onNewGraphicRequested);
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::graphicPageSelected,
            this, &MainWindow::onGraphicPageSelected);
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::graphicDeleteRequested,
            this, [this](int row) {
        // 侧边栏删除 Graphic → 找到对应标签页并关闭（触发完整清理链）
        const auto allTabs = m_editorArea->allTabWidgets();
        int graphicIdx = 0;
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                if (tw->tabText(i).contains("Graphic")) {
                    if (graphicIdx == row) {
                        m_editorArea->closeTab(tw, i);
                        return;
                    }
                    graphicIdx++;
                }
            }
        }
    });
    connect(m_sideBar->settingsPanel(), &SettingsPanel::settingsRequested,
            this, &MainWindow::onSettingsRequested);
    connect(m_sideBar->settingsPanel(), &SettingsPanel::themeChanged,
            this, [](const QString &name) {
        ThemeManager::instance()->applyTheme(name);
    });
    // 主题切换后刷新 ActivityBar 图标颜色
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            m_activityBar, &ActivityBar::refreshIcons);
    // 设备连接面板 — 点击设备条目打开标签页
    connect(m_sideBar->devicePanel(), &DevicePanel::deviceOpenRequested,
            this, &MainWindow::onOpenDeviceTab);
    // 设备连接面板 — 「＋新增设备」→ 插件市场标签页（搜索安装驱动，方案 §13.6）
    connect(m_sideBar->devicePanel(), &DevicePanel::addDeviceRequested,
            this, &MainWindow::onOpenMarketTab);

    // 分析配置面板 — 点击打开 flow 标签页
    connect(m_sideBar->analysisPanel(), &MeasurementSetupPanel::openMeasurementSetupRequested,
            this, [this]() { onOpenMeasurementSetup(); });

    // 右侧面板快捷按钮
    connect(m_rightPanel, &RightPanel::recordRequested, this, &MainWindow::onQuickRecord);
    connect(m_rightPanel, &RightPanel::stopRecordRequested, this, &MainWindow::onQuickStopRecord);
    connect(m_rightPanel, &RightPanel::playRequested, this, &MainWindow::onPlay);
    connect(m_rightPanel, &RightPanel::pauseRequested, this, &MainWindow::onPause);
    connect(m_rightPanel, &RightPanel::stopRequested, this, &MainWindow::onStop);
    connect(m_rightPanel, &RightPanel::clearTraceRequested, this, &MainWindow::onClear);
    connect(m_rightPanel, &RightPanel::autoScrollToggled, this, &MainWindow::onAutoScrollToggled);
    connect(m_rightPanel, &RightPanel::connectRequested, this, &MainWindow::onQuickConnect);
    connect(m_rightPanel, &RightPanel::disconnectRequested, this, &MainWindow::onQuickDisconnect);
    connect(m_rightPanel, &RightPanel::aiMessageSent, this, &MainWindow::onAiMessageSent);

    // 关联
    m_sideBar->dbcPanel()->setDbcManager(m_dbcManager);
    m_sideBar->graphicConfigPanel()->setGraphicView(m_graphicView);
    m_sideBar->devicePanel()->setSimulator(m_simulator);
    m_sideBar->devicePanel()->setDeviceManager(m_deviceManager);

    // P0/P1: 连接过滤预设管理器到 FilterBar
    if (m_filterPresets && m_traceTab) {
        auto *filterBar = m_traceTab->filterBar();
        if (filterBar)
            filterBar->setPresetManager(m_filterPresets);
    }

    // P0/P1: 连接书签管理器到 RightPanel
    if (m_bookmarkMgr && m_rightPanel) {
        m_rightPanel->setBookmarkManager(m_bookmarkMgr);
        connect(m_rightPanel, &RightPanel::bookmarkJumped,
                this, &MainWindow::onBookmarkJumped);
    }

    // 标签页变化 → 刷新侧边栏面板列表
    connect(m_editorArea, &SplitEditorArea::tabListChanged,
            this, &MainWindow::refreshPanelLists);
    // 标签页切换 → 同步侧边栏面板 + 更新标签名和统计
    connect(m_editorArea, &SplitEditorArea::currentChanged, this, [this](int) {
        auto *tabs = m_editorArea->activeTabWidget();
        if (tabs && tabs->currentIndex() >= 0) {
            QString text = tabs->tabText(tabs->currentIndex());
            m_tabLabel->setText(text);

            // 根据标签页文本同步侧边栏面板和活动栏
            ActivityBar::Activity act = ActivityBar::None;
            if (text.contains("设备连接"))
                act = ActivityBar::Device;
            else if (text.contains("Flow", Qt::CaseInsensitive))
                act = ActivityBar::Analysis;
            else if (text.contains("Trace"))
                act = ActivityBar::Trace;
            else if (text.contains("Graphic"))
                act = ActivityBar::Graphic;
            else if (text.contains("DBC"))
                act = ActivityBar::Dbc;
            else if (text.contains("发送") || text.contains("回放") ||
                     text.contains("录制") || text.contains("离线分析"))
                act = ActivityBar::Transceive;
            else if (text == QStringLiteral("扩展"))
                act = ActivityBar::Extensions;

            if (act != ActivityBar::None) {
                m_activityBar->setCurrentActivity(act);
                m_sideBar->showPanel(static_cast<int>(act));
            }
        }
        updateStatistics();
    });

    // BottomPanel 命令
    connect(m_bottomPanel, &BottomPanel::commandEntered,
            this, &MainWindow::onCommandEntered);

    // DBC 加载通知
    connect(m_dbcManager, &DbcManager::dbcLoaded, this, [this](const QString &name) {
        m_bottomPanel->appendOutput("DBC 已加载: " + name);
    });

    m_bottomPanel->appendOutput("openbus 启动完成");
    updateActions();
    refreshPanelLists();
    refreshPluginList();

    // ---- 工程管理 ----
    connect(m_sideBar->projectPanel(), &ProjectPanel::projectSwitched,
            this, &MainWindow::onProjectSwitched);
    connect(m_sideBar->projectPanel(), &ProjectPanel::projectCreated,
            this, &MainWindow::onProjectCreated);
    connect(m_sideBar->projectPanel(), &ProjectPanel::openProjectRequested,
            this, [this](const QString &path) {
        // 保存当前工程状态到状态快照
        auto &projs = m_sideBar->projectPanel()->projectsRef();
        int curIdx = m_sideBar->projectPanel()->currentIndex();
        captureProjectState();
        if (curIdx >= 0 && curIdx < projs.size())
            projs[curIdx].stateJson = ProjectManager::instance()->toJsonString();
        if (!ProjectManager::instance()->currentFilePath().isEmpty())
            ProjectManager::instance()->saveProject();
        if (ProjectManager::instance()->loadProject(path)) {
            applyProjectState();
            // 更新当前工程的 stateJson 并刷新树形列表
            int newIdx = m_sideBar->projectPanel()->currentIndex();
            if (newIdx >= 0 && newIdx < projs.size()) {
                projs[newIdx].stateJson = ProjectManager::instance()->toJsonString();
                m_sideBar->projectPanel()->refreshList();
            }
            m_bottomPanel->appendOutput(QStringLiteral("工程已加载: ") +
                                        ProjectManager::instance()->currentProjectName());
        }
    });
    connect(m_sideBar->projectPanel(), &ProjectPanel::saveProjectRequested,
            this, [this](const QString &path) {
        captureProjectState();
        if (ProjectManager::instance()->saveProject(path))
            m_bottomPanel->appendOutput(QStringLiteral("工程已保存: ") + path);
    });
    connect(m_sideBar->projectPanel(), &ProjectPanel::filePreviewRequested,
            this, &MainWindow::onFilePreviewRequested);

    // 自动加载上次工程
    QString lastProj = AppConfig::instance()->getString("project.lastPath", "");
    if (!lastProj.isEmpty() && QFile::exists(lastProj)) {
        if (ProjectManager::instance()->loadProject(lastProj)) {
            applyProjectState();
            // 同步工程面板：替换默认工程为上次加载的工程
            auto &projs = m_sideBar->projectPanel()->projectsRef();
            if (!projs.isEmpty()) {
                projs[0].name = ProjectManager::instance()->currentProjectName();
                projs[0].filePath = lastProj;
                projs[0].stateJson = ProjectManager::instance()->toJsonString();
                m_sideBar->projectPanel()->refreshList();
            }
        }
    }

    // 兜底：如果未加载工程（首次启动 / lastPath 为空），applyProjectState 未被调用，
    // 需确保 Trace1/Graphic1 默认标签页和 Flow 实例块存在，否则硬件连接后数据无处可去。
    if (m_traceInstances.isEmpty()) {
        auto *tab = new TraceTab(this);
        setupTraceTab(tab);
        openTab(tab, QStringLiteral("Trace1"));
        m_traceInstances["trace1"] = tab;
        m_traceTab = tab;
        m_traceCount = qMax(m_traceCount, 1);
        // Flow 视图中添加 trace1 实例块
        if (m_setupView)
            m_setupView->addModuleInstance("trace", "trace1", "Trace1");
    }
    if (m_graphicInstances.isEmpty()) {
        auto *gv = new GraphicView(this);
        openTab(gv, QStringLiteral("Graphic1"));
        linkGraphicCursor(gv);
        m_graphicInstances["graphic1"] = gv;
        m_graphicView = gv;
        m_sideBar->graphicConfigPanel()->setGraphicView(gv);
        m_graphicCount = qMax(m_graphicCount, 1);
        // Flow 视图中添加 graphic1 实例块
        if (m_setupView)
            m_setupView->addModuleInstance("graphic", "graphic1", "Graphic1");
    }
    if (m_setupView)
        m_setupView->rebuildScene();
}

MainWindow::~MainWindow() = default;

// ============================================================
//  菜单栏
// ============================================================

void MainWindow::createMenuBar()
{
    // ---- 文件 ----
    auto *fileMenu = menuBar()->addMenu("文件(&F)");

    m_openAction = new QAction("打开文件...", this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setToolTip("打开报文文件 (BLF/ASC/CSV/PCAP/TRC) 或 DBC 文件");
    fileMenu->addAction(m_openAction);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    auto *openProj = new QAction("打开工程...", this);
    openProj->setShortcut(QKeySequence("Ctrl+Shift+O"));
    fileMenu->addAction(openProj);
    connect(openProj, &QAction::triggered, this, &MainWindow::onOpenProject);

    auto *saveProj = new QAction("保存工程", this);
    saveProj->setShortcut(QKeySequence("Ctrl+Shift+S"));
    fileMenu->addAction(saveProj);
    connect(saveProj, &QAction::triggered, this, &MainWindow::onSaveProject);

    fileMenu->addSeparator();

    m_importAction = new QAction("导入日志文件...", this);
    m_importAction->setShortcut(QKeySequence("Ctrl+I"));
    m_importAction->setToolTip("导入 BLF/ASC/CSV 日志文件到 Trace");
    fileMenu->addAction(m_importAction);
    connect(m_importAction, &QAction::triggered, this, &MainWindow::onImportLog);

    fileMenu->addSeparator();
    fileMenu->addAction("退出(&Q)", QKeySequence("Alt+F4"), this, &QApplication::quit);

    // ---- 视图 ----
    auto *viewMenu = menuBar()->addMenu("视图(&V)");

    auto *toggleLeft = new QAction("左侧栏", this);
    toggleLeft->setCheckable(true);
    toggleLeft->setChecked(true);
    viewMenu->addAction(toggleLeft);
    connect(toggleLeft, &QAction::triggered, this, &MainWindow::toggleLeftDock);

    auto *toggleBottom = new QAction("底部栏", this);
    toggleBottom->setCheckable(true);
    toggleBottom->setChecked(false);
    viewMenu->addAction(toggleBottom);
    connect(toggleBottom, &QAction::triggered, this, &MainWindow::toggleBottomDock);

    auto *toggleRight = new QAction("右侧栏", this);
    toggleRight->setCheckable(true);
    toggleRight->setChecked(false);
    viewMenu->addAction(toggleRight);
    connect(toggleRight, &QAction::triggered, this, &MainWindow::toggleRightDock);

    viewMenu->addSeparator();
    viewMenu->addAction("重置布局", this, &MainWindow::resetLayout);

    // ---- 工具 ----
    // 原“工具集/协议”侧边栏功能已插件化（blf-converter / dbc-tool / bus-statistics /
    // uds-diagnostic / canopen-explorer），在“扩展”面板安装使用；内置工具保留在此菜单
    auto *toolsMenu = menuBar()->addMenu("工具(&T)");
    toolsMenu->addAction("Data Window", QKeySequence("Ctrl+Shift+D"),
                         this, &MainWindow::onOpenDataWindow);
    toolsMenu->addAction("I/O Graph", QKeySequence("Ctrl+Shift+G"),
                         this, &MainWindow::onOpenIOGraph);
    toolsMenu->addSeparator();
    toolsMenu->addAction("着色规则编辑器...", this, &MainWindow::onOpenColorRuleEditor);

    // ---- 工具操作 (不创建菜单, QAction 挂到主窗口, 快捷键仍然生效) ----
    m_recordAction = new QAction("录制", this);
    m_recordAction->setCheckable(true);
    m_recordAction->setShortcut(QKeySequence("Ctrl+R"));
    addAction(m_recordAction);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    m_playAction = new QAction("播放", this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    addAction(m_playAction);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_pauseAction = new QAction("暂停", this);
    addAction(m_pauseAction);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);

    m_stopAction = new QAction("停止", this);
    addAction(m_stopAction);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    m_clearAction = new QAction("清空 Trace", this);
    addAction(m_clearAction);
    connect(m_clearAction, &QAction::triggered, this, &MainWindow::onClear);

    m_autoScrollAction = new QAction("自动滚动", this);
    m_autoScrollAction->setCheckable(true);
    m_autoScrollAction->setChecked(true);
    addAction(m_autoScrollAction);
    connect(m_autoScrollAction, &QAction::toggled, this, &MainWindow::onAutoScrollToggled);

    m_simAction = new QAction("模拟器开关", this);
    m_simAction->setCheckable(true);
    addAction(m_simAction);
    connect(m_simAction, &QAction::toggled, this, [this](bool on) {
        if (on) m_simulator->start();
        else    m_simulator->stop();
    });

    // ---- 插件入口已移至侧边栏扩展面板 ----

    // ---- 帮助 ----
    auto *helpMenu = menuBar()->addMenu("帮助(&H)");

    helpMenu->addAction("关于 openbus", this, &MainWindow::showAboutDialog);
    helpMenu->addSeparator();
    helpMenu->addAction("文档", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus"));
    });
    helpMenu->addAction("官方网站", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus"));
    });
    helpMenu->addAction("Gitee 仓库", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus"));
    });
    helpMenu->addSeparator();
    helpMenu->addAction("报告问题", this, []() {
        QDesktopServices::openUrl(QUrl("https://gitee.com/jake_cai/openbus/issues"));
    });
    helpMenu->addAction("检查更新", this, &MainWindow::showCheckUpdate);
    helpMenu->addAction("发版记录", this, &MainWindow::showReleaseNotes);
    helpMenu->addSeparator();
    helpMenu->addAction("快捷键", this, &MainWindow::showShortcuts);
    helpMenu->addAction("许可证", this, &MainWindow::showLicenseDialog);
    helpMenu->addSeparator();
    helpMenu->addAction("商业合作", this, &MainWindow::showBusinessCoop);
}

// ============================================================
//  窗口控制按钮
// ============================================================

void MainWindow::createWindowButtons()
{
    auto *container = new QWidget(this);
    container->setObjectName("WindowButtons");
    container->setFixedHeight(30);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_minBtn = new QToolButton(container);
    m_minBtn->setObjectName("WinMinBtn");
    m_minBtn->setText("\u2500");
    m_minBtn->setFixedSize(46, 30);
    m_minBtn->setAutoRaise(true);
    m_minBtn->setToolTip("最小化");

    m_maxBtn = new QToolButton(container);
    m_maxBtn->setObjectName("WinMaxBtn");
    m_maxBtn->setText("\u25a1");
    m_maxBtn->setFixedSize(46, 30);
    m_maxBtn->setAutoRaise(true);
    m_maxBtn->setToolTip("最大化");

    m_closeBtn = new QToolButton(container);
    m_closeBtn->setObjectName("WinCloseBtn");
    m_closeBtn->setText("\u2715");
    m_closeBtn->setFixedSize(46, 30);
    m_closeBtn->setAutoRaise(true);
    m_closeBtn->setToolTip("关闭");

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    menuBar()->setCornerWidget(container, Qt::TopRightCorner);

    connect(m_minBtn, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_maxBtn, &QToolButton::clicked, this, [this]() {
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    connect(m_closeBtn, &QToolButton::clicked, this, &QWidget::close);
}

// ============================================================
//  停靠面板布局
// ============================================================

void MainWindow::createLayout()
{
    // ---- 左侧 Dock ----
    auto *leftContainer = new QWidget(this);
    leftContainer->setObjectName("LeftContainer");
    leftContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto *leftLayout = new QHBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_activityBar = new ActivityBar(this);
    m_sideBar = new SideBar(this);

    leftLayout->addWidget(m_activityBar);
    leftLayout->addWidget(m_sideBar, 1);

    m_leftDock = new QDockWidget("侧边栏", this);
    m_leftDock->setObjectName("LeftDock");
    m_leftDock->setWidget(leftContainer);
    m_leftDock->setFeatures(QDockWidget::DockWidgetMovable |
                            QDockWidget::DockWidgetClosable |
                            QDockWidget::DockWidgetFloatable);
    m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_leftDock->setTitleBarWidget(new QWidget());
    m_leftDock->setMinimumWidth(0);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // ---- 中央: 可拆分编辑器区域 ----
    m_editorArea = new SplitEditorArea(this);
    setCentralWidget(m_editorArea);

    // 默认标签页：Flow + 设备连接（其他不打开）
    onOpenMeasurementSetup();

    if (!m_deviceTab) {
        m_deviceTab = new DeviceConnectionTab(this);
        m_deviceTab->setSimulator(m_simulator);
        m_deviceTab->setDeviceManager(m_deviceManager);
        setupDeviceTab(m_deviceTab);
        connect(m_deviceTab, &QObject::destroyed, this, [this]() { m_deviceTab = nullptr; });
    }
    openTab(m_deviceTab, QStringLiteral("设备连接"));

    // ---- 右侧 Dock ----
    m_rightPanel = new RightPanel(this);
    m_rightPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_rightDock = new QDockWidget("右侧栏", this);
    m_rightDock->setObjectName("RightDock");
    m_rightDock->setWidget(m_rightPanel);
    m_rightDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetClosable |
                             QDockWidget::DockWidgetFloatable);
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_rightDock->setTitleBarWidget(new QWidget());
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    // ---- 底部 Dock ----
    m_bottomPanel = new BottomPanel(this);
    m_bottomPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_bottomDock = new QDockWidget("输出", this);
    m_bottomDock->setObjectName("BottomDock");
    m_bottomDock->setWidget(m_bottomPanel);
    m_bottomDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetClosable |
                              QDockWidget::DockWidgetFloatable);
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    m_bottomDock->setTitleBarWidget(new QWidget());
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);

    resizeDocks({m_leftDock}, {280}, Qt::Horizontal);
    resizeDocks({m_rightDock}, {260}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {200}, Qt::Vertical);

    // 默认隐藏右侧栏和底部栏
    m_rightDock->setVisible(false);
    m_bottomDock->setVisible(false);
}

// ============================================================
//  状态栏
// ============================================================

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel("就绪", this);
    m_connLabel = new QLabel("未连接", this);
    m_errorLabel = new QLabel("", this);
    m_tabLabel = new QLabel("Trace", this);
    m_rowCountLabel = new QLabel("0行", this);
    m_selectedLabel = new QLabel("选中0行", this);
    m_filterLabel = new QLabel("过滤0/0", this);
    m_frameCountLabel = new QLabel("0 帧", this);
    m_timeLabel = new QLabel("0.000s", this);

    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addWidget(m_connLabel);
    statusBar()->addWidget(m_errorLabel);
    statusBar()->addPermanentWidget(m_tabLabel);
    statusBar()->addPermanentWidget(m_rowCountLabel);
    statusBar()->addPermanentWidget(m_selectedLabel);
    statusBar()->addPermanentWidget(m_filterLabel);
    statusBar()->addPermanentWidget(m_frameCountLabel);
    statusBar()->addPermanentWidget(m_timeLabel);
}

// ============================================================
//  ActivityBar → 侧边栏 + 主标签页联动
// ============================================================

void MainWindow::onActivityChanged(int activity)
{
    m_sideBar->showPanel(activity);
    if (!m_sideBarVisible) {
        // 从收起状态展开：恢复 dock 宽度
        m_sideBar->setVisible(true);
        m_leftDock->setMinimumWidth(0);
        m_leftDock->setMaximumWidth(QWIDGETSIZE_MAX);
        m_leftDock->resize(m_savedDockWidth, m_leftDock->height());
        m_sideBarVisible = true;
    }

    // 联动主标签页
    if (activity == ActivityBar::Trace) {
        // 在所有拆分组中查找 Trace 标签页
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains("Trace")) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found)
            onOpenTraceTab();
    } else if (activity == ActivityBar::Graphic) {
        // 在所有拆分组中查找 Graphic 标签页
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains("Graphic")) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found)
            onNewGraphicRequested();
    } else if (activity == ActivityBar::Transceive) {
        // 收发面板：只切换侧边栏显示，不自动打开标签页
        // 用户点击侧边栏内的按钮才打开对应标签页
    } else if (activity == ActivityBar::Device) {
        // 切换到已存在的设备连接标签页
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains("设备连接")) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found && m_deviceTab)
            m_editorArea->addTab(m_deviceTab, QStringLiteral("设备连接"));
    } else if (activity == ActivityBar::Analysis) {
        onOpenMeasurementSetup();
    } else if (activity == ActivityBar::Extensions) {
        // 扩展管理：打开统一插件市场标签页（v2，方案 §13.5）
        const auto allTabs = m_editorArea->allTabWidgets();
        bool found = false;
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                if (tw->tabText(i).contains(QStringLiteral("插件市场"))) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (found) {
            if (m_marketTab)
                m_marketTab->refreshInstalled();
        } else {
            onOpenMarketTab();
        }
    }
}

void MainWindow::onActivityToggled(int)
{
    if (m_sideBarVisible) {
        // 收起：保存当前宽度，将 dock 缩小到仅 ActivityBar 宽度
        m_savedDockWidth = m_leftDock->width();
        m_sideBar->setVisible(false);
        m_leftDock->setFixedWidth(m_activityBar->width());
    } else {
        // 展开：恢复保存的宽度
        m_sideBar->setVisible(true);
        m_leftDock->setMinimumWidth(0);
        m_leftDock->setMaximumWidth(QWIDGETSIZE_MAX);
        m_leftDock->resize(m_savedDockWidth, m_leftDock->height());
    }
    m_sideBarVisible = !m_sideBarVisible;
}

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
        // 优先从离线分析标签页加载
        if (m_offlineTab && !m_offlineTab->isEmpty()) {
            QStringList paths = m_offlineTab->filePaths();
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
    // 清除所有 Trace 模型和 Graphic 视图
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            auto *gv = qobject_cast<GraphicView *>(tw->widget(i));
            if (gv) gv->clearData();
            auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
            if (tt) {
                tt->clearTrace();
                tt->frameInfo()->clear();
                tt->signalDecode()->clear();
            }
        }
    }
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
        // 清除所有 Trace 和 Graphic 视图
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                auto *gv = qobject_cast<GraphicView *>(tw->widget(i));
                if (gv) gv->clearData();
                auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
                if (tt) tt->clearTrace();
            }
        }
        m_bottomPanel->appendOutput(QString("已加载: %1 (%2 帧, %3s)")
            .arg(fi.fileName()).arg(m_player->totalFrames())
            .arg(m_player->totalTime(), 0, 'f', 2));
        if (m_playbackTab)
            m_playbackTab->setFileInfo(fi.fileName(), m_player->totalFrames(), m_player->totalTime());
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

    // 找到活跃的 TraceTab（当前页或第一个找到的）
    TraceTab *traceTab = qobject_cast<TraceTab *>(m_editorArea->currentWidget());
    if (!traceTab) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                traceTab = qobject_cast<TraceTab *>(tw->widget(i));
                if (traceTab) break;
            }
            if (traceTab) break;
        }
    }

    if (traceTab) {
        traceTab->appendFrames(frames);
        if (m_autoScroll)
            traceTab->traceView()->scrollToBottom();
    }

    const QFileInfo fi(path);
    const int count = traceTab ? traceTab->frameCount() : frames.size();
    m_bottomPanel->appendOutput(QString("导入完成: %1 (%2 帧, 格式: %3)")
        .arg(fi.fileName()).arg(frames.size()).arg(importer->formatName()));
    m_frameCountLabel->setText(QString::number(count) + QStringLiteral(" 帧"));
    m_rowCountLabel->setText(QString::number(count) + QStringLiteral("行"));
    m_statusLabel->setText(QString("已导入 %1").arg(fi.fileName()));
}

void MainWindow::onAutoScrollToggled(bool on)
{
    m_autoScroll = on;
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
            if (tt) tt->traceView()->setAutoScrollEnabled(on);
        }
    }
}

// ============================================================
//  数据流
// ============================================================

void MainWindow::onFrameReceived(const CanFrame &frame)
{
    // 测量未运行时直接断流 — 不再向任何数据块分发帧
    if (!m_measurementRunning)
        return;

    // 发送到所有启用的 Graphic 视图，仅向运行中的 Trace 标签页追加帧
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            auto *gv = qobject_cast<GraphicView *>(tw->widget(i));
            if (gv) {
                // 检查 flow 块是否启用（未设置 property 默认为 true）
                bool flowEnabled = gv->property("flowEnabled").toBool();
                if (!gv->property("flowEnabled").isValid() || flowEnabled)
                    gv->onFrame(frame);
            }
            auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
            if (tt && tt->isRunning()) {
                tt->appendFrame(frame);
                if (m_autoScroll && !tt->isOverwriteMode())
                    tt->traceView()->scrollToBottom();
            }
            auto *msv = qobject_cast<MeasurementSetupView *>(tw->widget(i));
            if (msv) msv->onFrame(frame);
        }
    }
    if (m_recording)
        m_recorder->recordFrame(frame);
    // 发送到总线统计引擎
    if (m_busStats)
        m_busStats->onFrame(frame);
    // 发送到 Data Window
    if (m_dataWindow)
        m_dataWindow->onFrame(frame);
    // 发送到 I/O Graph
    if (m_ioGraph)
        m_ioGraph->onFrame(frame);
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
//  Trace 选择
// ============================================================

void MainWindow::onTraceSelectionChanged()
{
    auto *tv = qobject_cast<TraceView *>(sender());
    if (!tv) {
        m_selectedLabel->setText("选中0行");
        return;
    }
    const CanFrame *frame = tv->selectedFrame();
    m_selectedLabel->setText(frame ? "选中1行" : "选中0行");
}

void MainWindow::onFrameDoubleClicked(const CanFrame &frame)
{
    QString filter = QString("id == %1").arg(CanUtils::formatId(frame.id, frame.extended));

    auto *tabs = m_editorArea->activeTabWidget();

    // 在当前 Trace 标签页的过滤栏设置过滤
    if (tabs) {
        auto *activeTrace = qobject_cast<TraceTab *>(tabs->currentWidget());
        if (activeTrace) {
            auto *edit = activeTrace->filterBar()->findChild<QLineEdit *>();
            if (edit) edit->setText(filter);
            activeTrace->setFilterExpression(filter);
        }
    }

    // 添加信号到当前或最后的 Graphic 视图
    GraphicView *targetGv = nullptr;
    if (tabs) {
        targetGv = qobject_cast<GraphicView *>(tabs->currentWidget());
        if (!targetGv) {
            for (int i = tabs->count() - 1; i >= 0; --i) {
                auto *gv = qobject_cast<GraphicView *>(tabs->widget(i));
                if (gv) { targetGv = gv; break; }
            }
        }
    }
    if (!targetGv) {
        targetGv = new GraphicView(this);
        openTab(targetGv, QString("Graphic%1").arg(++m_graphicCount));
        linkGraphicCursor(targetGv);
    }

    GraphicView::Signal sig;
    sig.name = QString("ID_%1[0]").arg(frame.id, 0, 16).toUpper();
    sig.canId = frame.id & 0x1FFFFFFF;
    sig.extended = frame.extended;
    sig.dbcSig.name = sig.name;
    sig.dbcSig.startBit = 0;
    sig.dbcSig.bitLength = 8;
    sig.dbcSig.littleEndian = true;
    sig.dbcSig.factor = 1.0;
    sig.dbcSig.offset = 0.0;
    targetGv->addSignal(sig);

    // 切换到 Graphic 标签页
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

// ============================================================
//  回放
// ============================================================

void MainWindow::onPlayerProgress(int cur, int total, double curTime, double totalTime)
{
    if (m_playbackTab)
        m_playbackTab->setProgress(cur, total, curTime, totalTime);
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

    GraphicView::Signal gsig;
    gsig.name = signalName;
    gsig.canId = canId;
    gsig.extended = msg && (msg->id > 0x7FF);
    gsig.dbcSig = *sig;   // 完整复制 DBC 信号定义（startBit/bitLength/factor/offset/signed 等）

    // 添加到当前或最后的 Graphic 视图
    GraphicView *targetGv = nullptr;
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        targetGv = qobject_cast<GraphicView *>(tabs->currentWidget());
        if (!targetGv) {
            for (int i = tabs->count() - 1; i >= 0; --i) {
                auto *gv = qobject_cast<GraphicView *>(tabs->widget(i));
                if (gv) { targetGv = gv; break; }
            }
        }
    }
    if (!targetGv) {
        targetGv = new GraphicView(this);
        openTab(targetGv, QString("Graphic%1").arg(++m_graphicCount));
        linkGraphicCursor(targetGv);
    }
    targetGv->addSignal(gsig);

    m_bottomPanel->appendOutput(QString("已添加信号: %1 (ID=0x%2)")
        .arg(signalName).arg(canId, 0, 16).toUpper());
}

void MainWindow::onDbcFileClicked(const QString &fileName)
{
    // 打开 DBC 详情标签页
    auto *dbcTab = new DbcDetailTab(fileName, m_dbcManager, this);
    connect(dbcTab, &DbcDetailTab::signalDoubleClicked,
            this, &MainWindow::onSignalDoubleClicked);
    connect(dbcTab, &DbcDetailTab::signalAddToGraphic,
            this, &MainWindow::onSignalDoubleClicked);
    connect(dbcTab, &DbcDetailTab::signalAddToTrace,
            this, &MainWindow::onSignalAddToTrace);
    openTab(dbcTab, "DBC: " + fileName);
}

void MainWindow::onSignalAddToTrace(quint32 canId, const QString &signalName)
{
    // 查找现有 TraceTab，没有则新建
    TraceTab *targetTrace = nullptr;
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        targetTrace = qobject_cast<TraceTab *>(tabs->currentWidget());
        if (!targetTrace) {
            const auto allTabs = m_editorArea->allTabWidgets();
            for (auto *tw : allTabs) {
                for (int i = tw->count() - 1; i >= 0; --i) {
                    auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
                    if (tt) { targetTrace = tt; break; }
                }
                if (targetTrace) break;
            }
        }
    }
    if (!targetTrace) {
        targetTrace = new TraceTab(this);
        setupTraceTab(targetTrace);
        openTab(targetTrace, QString("Trace%1").arg(++m_traceCount));
    } else {
        // 切换到已有 TraceTab
        if (tabs) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->widget(i) == targetTrace) {
                    tabs->setCurrentIndex(i);
                    m_tabLabel->setText(tabs->tabText(i));
                    break;
                }
            }
        }
    }

    // 设置过滤器：只显示该 CAN ID 的帧
    QString filter = QString("id == 0x%1").arg(canId, 0, 16).toUpper();
    if (!targetTrace->setFilterExpression(filter))
        m_bottomPanel->addProblem(0, "Filter", "语法错误: " + filter);

    m_bottomPanel->appendOutput(QString("已添加信号到 Trace: %1 (ID=0x%2)")
        .arg(signalName).arg(canId, 0, 16).toUpper());
}

// ============================================================
//  侧边栏入口
// ============================================================

void MainWindow::setupTraceTab(TraceTab *tab)
{
    tab->setDbcManager(m_dbcManager);

    auto *filterBar = tab->filterBar();

    // 每个标签页独立过滤
    connect(filterBar, &FilterBar::filterApplied, this, [this, tab](const QString &filter) {
        if (!tab->setFilterExpression(filter))
            m_bottomPanel->addProblem(0, "Filter", "语法错误: " + filter);
    });
    connect(filterBar, &FilterBar::filterCleared, this, [tab]() {
        tab->clearFilter();
    });

    auto *traceView = tab->traceView();
    connect(traceView, &TraceView::frameDoubleClicked,
            this, &MainWindow::onFrameDoubleClicked);
    connect(traceView, &TraceView::frameSelected,
            this, &MainWindow::onTraceSelectionChanged);
    connect(traceView, &TraceView::frameAddToGraphic,
            this, [this](const CanFrame &frame) {
        // 查找该帧对应的 DBC 信号，添加到当前或最后的 Graphic
        const DbcMessage *msg = m_dbcManager->findMessage(frame.id);
        if (!msg) {
            m_bottomPanel->appendOutput(
                QStringLiteral("未找到 ID=0x%1 对应的 DBC 报文定义")
                    .arg(frame.id, 0, 16).toUpper());
            return;
        }
        // 查找或创建目标 Graphic 视图
        GraphicView *targetGv = nullptr;
        auto *tabs = m_editorArea->activeTabWidget();
        if (tabs) {
            targetGv = qobject_cast<GraphicView *>(tabs->currentWidget());
            if (!targetGv) {
                for (int i = tabs->count() - 1; i >= 0; --i) {
                    auto *gv = qobject_cast<GraphicView *>(tabs->widget(i));
                    if (gv) { targetGv = gv; break; }
                }
            }
        }
        if (!targetGv) {
            targetGv = new GraphicView(this);
            openTab(targetGv, QString("Graphic%1").arg(++m_graphicCount));
            linkGraphicCursor(targetGv);
        }
        // 添加该报文的所有信号到 Graphic
        for (const auto &sig : msg->signalList) {
            GraphicView::Signal gsig;
            gsig.name = sig.name;
            gsig.canId = frame.id;
            gsig.extended = frame.extended;
            gsig.dbcSig = sig;
            targetGv->addSignal(gsig);
        }
        m_bottomPanel->appendOutput(
            QStringLiteral("已添加 %1 个信号到 Graphic (ID=0x%2)")
                .arg(msg->signalList.size()).arg(frame.id, 0, 16).toUpper());
    });
    connect(traceView, &TraceView::clearFilterRequested,
            this, [tab]() {
        tab->clearAllFilters();  // 右键清除 = 主过滤 + 列过滤全部清除
    });

    // 文件拖放加载完成
    connect(tab, &TraceTab::fileLoaded, this, [this, tab](int count) {
        if (count < 0)
            m_bottomPanel->appendOutput("文件加载失败");
        else {
            m_bottomPanel->appendOutput(QString("已加载 %1 帧").arg(count));
            m_frameCountLabel->setText(QString::number(count) + " 帧");
        }
    });
}

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
    m_marketTab = new MarketTab(this);

    // 插件操作请求 → PluginManager（沿用原 ExtensionsTab 的连接）
    connect(m_marketTab, &MarketTab::pluginActivateRequested,
            this, [this](const QString &name) {
        if (m_pluginManager) m_pluginManager->reactivatePlugin(name);
    });
    connect(m_marketTab, &MarketTab::pluginDeactivateRequested,
            this, [this](const QString &name) {
        if (m_pluginManager) m_pluginManager->deactivatePlugin(name);
        if (m_pluginManager) emit m_pluginManager->pluginListChanged();
    });
    connect(m_marketTab, &MarketTab::pluginToggleRequested,
            this, [this](const QString &name, bool enable) {
        if (m_pluginManager) m_pluginManager->setPluginEnabled(name, enable);
    });

    // 标签页被关闭后 widget 被删除 → 置空指针，避免悬空引用
    connect(m_marketTab, &QObject::destroyed, this, [this]() {
        m_marketTab = nullptr;
    });
}

void MainWindow::onOpenMarketTab()
{
    // 标签页可能已被关闭并删除，需要重建
    if (!m_marketTab)
        setupMarketTab();
    openTab(m_marketTab, QStringLiteral("插件市场"));
    m_marketTab->refreshInstalled();
    // ＋新增设备跳转后直接聚焦搜索（方案 §13.6）
    m_marketTab->focusSearch();
}

void MainWindow::openPluginDetail(const QString &name)
{
    // 多个插件共用一个详情标签页
    if (!m_pluginDetailPage) {
        m_pluginDetailPage = new PluginDetailPage(this);
        // 标签页被关闭后 widget 被删除 → 置空指针，下次点击重建
        connect(m_pluginDetailPage, &QObject::destroyed, this, [this]() {
            m_pluginDetailPage = nullptr;
        });
    }
    m_pluginDetailPage->showPlugin(name);

    // 已打开 → 复用并更新标签文本；否则在编辑区新开
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->widget(i) == m_pluginDetailPage) {
                tabs->setTabText(i, name);
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(name);
                return;
            }
        }
    }
    openTab(m_pluginDetailPage, name);
}

void MainWindow::onOpenTraceTab()
{
    auto *tab = new TraceTab(this);
    setupTraceTab(tab);
    openTab(tab, QString("Trace%1").arg(++m_traceCount));
}

void MainWindow::onTracePageSelected(int row)
{
    const auto allTabs = m_editorArea->allTabWidgets();
    int traceIdx = 0;
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("Trace")) {
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
    // 在所有拆分组中查找已有的发送标签页
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("发送")) {
                tw->setCurrentIndex(i);
                m_tabLabel->setText(tw->tabText(i));
                return;
            }
        }
    }
    // 未找到则创建新的
    m_sendTab = new SignalSendTab(this);
    m_sendTab->setDbcManager(m_dbcManager);
    setupSendTab(m_sendTab);
    connect(m_sendTab, &QObject::destroyed, this, [this]() { m_sendTab = nullptr; });
    openTab(m_sendTab, "发送");
}

void MainWindow::onOpenPlaybackTab()
{
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("回放")) {
                tw->setCurrentIndex(i);
                m_tabLabel->setText(tw->tabText(i));
                return;
            }
        }
    }
    // 未找到则创建新的
    m_playbackTab = new PlaybackTab(this);
    setupPlaybackTab(m_playbackTab);
    connect(m_playbackTab, &QObject::destroyed, this, [this]() { m_playbackTab = nullptr; });
    openTab(m_playbackTab, "回放");
}

void MainWindow::onOpenOfflineAnalysisTab()
{
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains(QStringLiteral("离线分析"))) {
                tw->setCurrentIndex(i);
                m_tabLabel->setText(tw->tabText(i));
                return;
            }
        }
    }
    // 未找到则创建新的
    m_offlineTab = new OfflineAnalysisTab(this);
    setupOfflineAnalysisTab(m_offlineTab);
    connect(m_offlineTab, &QObject::destroyed, this, [this]() { m_offlineTab = nullptr; });
    openTab(m_offlineTab, QStringLiteral("离线分析"));
}

void MainWindow::onOpenRecordTab()
{
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("录制")) {
                tw->setCurrentIndex(i);
                m_tabLabel->setText(tw->tabText(i));
                return;
            }
        }
    }
    // 未找到则创建新的
    m_recordTab = new RecordTab(this);
    setupRecordTab(m_recordTab);
    connect(m_recordTab, &QObject::destroyed, this, [this]() { m_recordTab = nullptr; });
    openTab(m_recordTab, "录制");
}

// ============================================================
//  标签页 setup 方法 — 提取自构造函数，支持关闭后重建
// ============================================================

void MainWindow::setupSendTab(SignalSendTab *tab)
{
    // 发送单帧
    connect(tab, &SignalSendTab::sendSingleRequested, this, [this](quint32 id, const QByteArray &data) {
        CanFrame frame;
        frame.id = id;
        frame.dlc = CanFrame::lengthToDlc(data.size());
        frame.data = data;
        frame.direction = CanFrame::Tx;

        if (m_deviceManager->sendFrame(frame)) {
            m_bottomPanel->appendOutput(QString("发送: ID=0x%1, DLC=%2")
                .arg(id, 0, 16).toUpper().arg(data.size()));
        } else {
            m_bottomPanel->appendOutput(QString("发送失败 (设备未连接或为模拟器): ID=0x%1")
                .arg(id, 0, 16).toUpper());
        }
    });

    // 发送行（单次或周期）
    connect(tab, &SignalSendTab::sendRowRequested, this,
        [this](int row, quint32 id, const QByteArray &data, int period, int count) {
        CanFrame frame;
        frame.id = id;
        frame.dlc = CanFrame::lengthToDlc(data.size());
        frame.data = data;
        frame.direction = CanFrame::Tx;

        // 先发送一帧
        if (m_deviceManager->sendFrame(frame)) {
            m_bottomPanel->appendOutput(QString("发送行%1: ID=0x%2, DLC=%3")
                .arg(row + 1).arg(id, 0, 16).toUpper().arg(data.size()));
        } else {
            m_bottomPanel->appendOutput(QString("发送失败 (设备未连接或为模拟器): ID=0x%1")
                .arg(id, 0, 16).toUpper());
        }

        // 周期发送
        if (period > 0) {
            // 停止该行已有的定时器
            auto it = m_periodicSenders.find(row);
            if (it != m_periodicSenders.end()) {
                it.value()->stop();
                it.value()->deleteLater();
                m_periodicSenders.erase(it);
            }

            auto *timer = new QTimer(this);
            timer->setInterval(period);
            int remaining = count;  // 0 = 无限
            connect(timer, &QTimer::timeout, this, [this, id, data, row, count, timer, remaining]() mutable {
                CanFrame f;
                f.id = id;
                f.dlc = CanFrame::lengthToDlc(data.size());
                f.data = data;
                f.direction = CanFrame::Tx;
                m_deviceManager->sendFrame(f);

                if (count > 0) {
                    --remaining;
                    if (remaining <= 0) {
                        timer->stop();
                        timer->deleteLater();
                        m_periodicSenders.remove(row);
                        m_bottomPanel->appendOutput(
                            QString("行%1 周期发送完成 (%2 次)").arg(row + 1).arg(count));
                    }
                }
            });
            timer->start();
            m_periodicSenders[row] = timer;
        }
    });

    // 停止单行
    connect(tab, &SignalSendTab::stopRowRequested, this, [this](int row) {
        auto it = m_periodicSenders.find(row);
        if (it != m_periodicSenders.end()) {
            it.value()->stop();
            it.value()->deleteLater();
            m_periodicSenders.erase(it);
            m_bottomPanel->appendOutput(QString("停止行%1 周期发送").arg(row + 1));
        }
    });

    // 全部停止（安全网：确保所有定时器都停止）
    connect(tab, &SignalSendTab::stopAllRequested, this, [this]() {
        for (auto *t : m_periodicSenders) {
            t->stop();
            t->deleteLater();
        }
        m_periodicSenders.clear();
    });
}

void MainWindow::setupPlaybackTab(PlaybackTab *tab)
{
    connect(tab, &PlaybackTab::playRequested, this, &MainWindow::onPlay);
    connect(tab, &PlaybackTab::pauseRequested, this, &MainWindow::onPause);
    connect(tab, &PlaybackTab::stopRequested, this, &MainWindow::onStop);
    connect(tab, &PlaybackTab::speedChanged, this, &MainWindow::onSpeedChanged);
    connect(tab, &PlaybackTab::seekChanged, this, &MainWindow::onSeekChanged);
    connect(tab, &PlaybackTab::loopToggled, m_player, &Player::setLoop);
    connect(tab, &PlaybackTab::autoScrollToggled, this, &MainWindow::onAutoScrollToggled);
    connect(tab, &PlaybackTab::fileLoaded, this, [this, tab](const QString &path) {
        QFileInfo fi(path);
        if (!m_player->load(path)) {
            QMessageBox::warning(this, "回放", "无法加载: " + path);
            m_bottomPanel->addProblem(1, "Player", "无法加载: " + path);
            return;
        }
        // 清除所有 Trace 和 Graphic 视图
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                auto *gv = qobject_cast<GraphicView *>(tw->widget(i));
                if (gv) gv->clearData();
                auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
                if (tt) tt->clearTrace();
            }
        }
        m_bottomPanel->appendOutput(QString("已加载: %1 (%2 帧, %3s)")
            .arg(fi.fileName()).arg(m_player->totalFrames())
            .arg(m_player->totalTime(), 0, 'f', 2));
        tab->setFileInfo(fi.fileName(), m_player->totalFrames(), m_player->totalTime());
        updateActions();
    });
}

void MainWindow::setupOfflineAnalysisTab(OfflineAnalysisTab *tab)
{
    // 离线分析标签页是纯文件列表管理，不直接加载文件
    // 文件加载由 Flow 界面点击"开始"时统一处理（measurementToggled）
    Q_UNUSED(tab);
}

void MainWindow::setupRecordTab(RecordTab *tab)
{
    connect(tab, &RecordTab::recordToggled, this, [this, tab](bool on) {
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

            if (!m_recorder->start(path)) {
                QMessageBox::warning(this, "录制", "无法创建文件: " + path +
                    "\n请检查路径是否有效、磁盘空间是否足够。");
                tab->setRecording(false);
                m_bottomPanel->addProblem(1, "Recorder", "无法创建录制文件: " + path);
                return;
            }
            m_bottomPanel->appendOutput("开始录制: " + path);
        } else {
            m_recorder->stop();
        }
    });

    // 暂停/恢复录制
    connect(tab, &RecordTab::pauseRequested, this, [this, tab](bool paused) {
        if (paused) {
            m_recorder->pause();
            m_bottomPanel->appendOutput("录制已暂停");
        } else {
            m_recorder->resume();
            m_bottomPanel->appendOutput("录制已恢复");
        }
    });

    // P1: 触发录制
    connect(tab, &RecordTab::triggerRecordingRequested,
            this, &MainWindow::onTriggerRecording);

    // 触发录制停止
    connect(tab, &RecordTab::triggerRecordingStopped, this, [this]() {
        if (m_triggerRecorder)
            m_triggerRecorder->stop();
    });
}

void MainWindow::onOpenDeviceTab(int deviceKind, int devIndex, const QString &deviceName, int deviceType)
{
    // 查找已有的设备连接标签页
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("设备连接")) {
                tw->setCurrentIndex(i);
                m_tabLabel->setText(tw->tabText(i));
                if (m_deviceTab)
                    m_deviceTab->setDevice(deviceKind, devIndex, deviceName, deviceType);
                return;
            }
        }
    }

    // 未找到则创建新的
    if (!m_deviceTab) {
        m_deviceTab = new DeviceConnectionTab(this);
        m_deviceTab->setSimulator(m_simulator);
        m_deviceTab->setDeviceManager(m_deviceManager);
        setupDeviceTab(m_deviceTab);
        connect(m_deviceTab, &QObject::destroyed, this, [this]() { m_deviceTab = nullptr; });
    }
    m_deviceTab->setDevice(deviceKind, devIndex, deviceName, deviceType);
    openTab(m_deviceTab, QStringLiteral("设备连接"));
}

void MainWindow::setupDeviceTab(DeviceConnectionTab *tab)
{
    // V2 信号 — 真实硬件连接
    connect(tab, &DeviceConnectionTab::deviceConnectRequestedV2,
            this, [this](int devKind, int devIndex, int channel,
                         int arbBaud, int dataBaud, bool canFd, int devSubType) {
        auto kind = static_cast<CanDeviceManager::DeviceKind>(devKind);
        m_deviceManager->configure(kind, devIndex, channel,
                                   arbBaud, dataBaud, canFd, devSubType);
        m_deviceManager->start();
    });

    // 连接成功 — 仅连接设备，不启动数据流
    connect(tab, &DeviceConnectionTab::deviceConnectRequested,
            this, [this](const QString &name, int) {
        // 真实设备模式下，检查设备是否成功启动
        if (m_deviceManager->isRealDevice() && !m_deviceManager->isRunning()) {
            m_bottomPanel->appendOutput(
                QStringLiteral("⚠ 设备连接失败: %1").arg(name));
            return;
        }
        if (!m_deviceManager->isRealDevice())
            m_connLabel->setText(QStringLiteral("🔗 已连接"));
        // 不自动启动数据流 — 需在 Flow 页面点击“开始”后才向 Trace/Graphic 分发数据
        m_bottomPanel->appendOutput(
            QStringLiteral("✅ 设备已连接: %1 (请在 Flow 页面点击开始启动数据流)").arg(name));
    });
    // 断开 — 停止数据流
    connect(tab, &DeviceConnectionTab::deviceDisconnectRequested,
            this, [this]() {
        m_connLabel->setText(QStringLiteral("🔗 未连接"));
        m_measurementRunning = false;
        m_simulator->stop();
        m_deviceManager->stop();
        for (auto *w : m_traceInstances) {
            auto *traceTab = qobject_cast<TraceTab *>(w);
            if (traceTab)
                traceTab->setRunning(false);
        }
        m_bottomPanel->appendOutput(QStringLiteral("■ 数据流已停止"));
    });
}

void MainWindow::linkGraphicCursor(GraphicView *gv)
{
    // 与所有已打开的 GraphicView 建立游标联动（双向 + 去重，
    // 视图关闭时 Qt 自动断开连接）
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            auto *other = qobject_cast<GraphicView *>(tw->widget(i));
            if (!other || other == gv)
                continue;
            connect(gv, &GraphicView::cursorMoved, other, &GraphicView::onSyncCursor,
                    Qt::UniqueConnection);
            connect(other, &GraphicView::cursorMoved, gv, &GraphicView::onSyncCursor,
                    Qt::UniqueConnection);
        }
    }
}

void MainWindow::onNewGraphicRequested()
{
    auto *gv = new GraphicView(this);
    openTab(gv, QString("Graphic%1").arg(++m_graphicCount));
    linkGraphicCursor(gv);
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

    // 创建新的 flow 标签页
    auto *view = new MeasurementSetupView(this);

    // 设置已加载的 DBC 文件列表
    QStringList dbcFiles;
    for (const auto &f : m_dbcManager->files())
        dbcFiles << f.fileName;
    view->setDbcFiles(dbcFiles);

    // DBC 文件加载时同步更新视图中的列表
    connect(m_dbcManager, &DbcManager::dbcLoaded, view, [view, this](const QString &) {
        QStringList files;
        for (const auto &f : m_dbcManager->files())
            files << f.fileName;
        view->setDbcFiles(files);
    });
    connect(m_dbcManager, &DbcManager::dbcUnloaded, view, [view, this](const QString &) {
        QStringList files;
        for (const auto &f : m_dbcManager->files())
            files << f.fileName;
        view->setDbcFiles(files);
    });

    // 记录当前 flow 视图，用于实例跟踪
    m_setupView = view;
    connect(view, &QObject::destroyed, this, [this]() { m_setupView = nullptr; });

    // 连接信号
    connect(view, &MeasurementSetupView::sourceChanged,
            this, [this](int src) {
        if (src == static_cast<int>(MeasurementSetupView::Source::File)) {
            m_simulator->stop();
            m_deviceManager->stop();
            m_bottomPanel->appendOutput("数据源切换：离线分析");
        } else {
            m_player->stop();
            m_bottomPanel->appendOutput("数据源切换：硬件实时");
        }
    });
    connect(view, &MeasurementSetupView::fileBrowseRequested,
            this, [this]() {
        // 打开离线分析标签页（文件选择在该标签页内完成）
        onOpenOfflineAnalysisTab();
    });
    connect(view, &MeasurementSetupView::measurementToggled,
            this, [this, view](bool running) {
        m_measurementRunning = running;
        if (running) {
            m_receivedFrameCount = 0;  // 重置帧计数器
            m_bottomPanel->appendOutput(" 测量开始");
            if (view->currentSource() == MeasurementSetupView::Source::Hardware) {
                // 硬件模式：根据 DevicePanel 选中设备决定数据源
                if (m_deviceManager->isRunning()) {
                    // 真实硬件已连接，无需重复启动
                } else {
                    m_simulator->start();
                }
            } else {
                // 离线分析模式：从离线分析标签页加载所有文件，合并后送入 Player
                m_player->stop();
                QStringList paths = (m_offlineTab && !m_offlineTab->isEmpty())
                                    ? m_offlineTab->filePaths() : QStringList{};

                if (paths.isEmpty()) {
                    // 无文件 → 回退到文件选择框
                    onOpenFile();
                    if (!m_player->isLoaded()) return;
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
                        return;
                    }
                    // 按时间戳排序合并帧
                    std::sort(allFrames.begin(), allFrames.end(),
                              [](const CanFrame &a, const CanFrame &b) {
                                  return a.timestamp < b.timestamp;
                              });
                    m_player->loadFrames(allFrames);
                    m_bottomPanel->appendOutput(
                        QStringLiteral("✅ 共加载 %1 个文件, %2 帧")
                            .arg(loadedNames.size()).arg(allFrames.size()));
                }

                // 清除所有 Trace 和 Graphic 视图
                const auto allTabs = m_editorArea->allTabWidgets();
                for (auto *tw : allTabs) {
                    for (int i = 0; i < tw->count(); ++i) {
                        auto *gv = qobject_cast<GraphicView *>(tw->widget(i));
                        if (gv) gv->clearData();
                        auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
                        if (tt) tt->clearTrace();
                    }
                }
                m_player->play();
            }
            // 所有已启用的 Trace 实例自动开始接收数据（遵循 Flow 块使能状态）
            for (auto it = m_traceInstances.begin(); it != m_traceInstances.end(); ++it) {
                auto *tab = qobject_cast<TraceTab *>(it.value());
                if (tab)
                    tab->setRunning(view->isBlockEnabled(it.key()));
            }
        } else {
            m_bottomPanel->appendOutput("■ 测量停止");
            m_simulator->stop();
            m_deviceManager->stop();
            m_player->stop();
            for (auto *w : m_traceInstances) {
                auto *tab = qobject_cast<TraceTab *>(w);
                if (tab) tab->setRunning(false);
            }
        }
    });
    connect(view, &MeasurementSetupView::moduleToggled,
            this, [this](const QString &blockId, const QString &name, bool enabled) {
        m_bottomPanel->appendOutput(QString("模块 %1 %2")
                                    .arg(name).arg(enabled ? "已启用" : "已禁用"));
        // 根据 blockId 控制对应实例的数据接收
        if (blockId.startsWith("trace")) {
            auto *tab = qobject_cast<TraceTab *>(m_traceInstances.value(blockId));
            if (tab) tab->setRunning(enabled && m_measurementRunning);
        } else if (blockId.startsWith("graphic")) {
            auto *gv = qobject_cast<GraphicView *>(m_graphicInstances.value(blockId));
            if (gv) gv->setProperty("flowEnabled", enabled);
        }
    });
    connect(view, &MeasurementSetupView::realBlockClicked,
            this, [this]() {
        // 跳转到设备连接标签页（已存在则切换，不存在则创建）
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                if (tw->tabText(i).contains(QStringLiteral("设备连接"))) {
                    tw->setCurrentIndex(i);
                    m_tabLabel->setText(tw->tabText(i));
                    return;
                }
            }
        }
        if (!m_deviceTab) {
            m_deviceTab = new DeviceConnectionTab(this);
            m_deviceTab->setSimulator(m_simulator);
            m_deviceTab->setDeviceManager(m_deviceManager);
            setupDeviceTab(m_deviceTab);
            connect(m_deviceTab, &QObject::destroyed, this, [this]() { m_deviceTab = nullptr; });
        }
        openTab(m_deviceTab, QStringLiteral("设备连接"));
    });
    connect(view, &MeasurementSetupView::fileBlockClicked,
            this, [this]() {
        // 双击 File 块 → 跳转到离线分析标签页
        onOpenOfflineAnalysisTab();
    });
    connect(view, &MeasurementSetupView::moduleOpened,
            this, [this, view](const QString &moduleId, const QString &instanceId) {
        if (moduleId == "trace") {
            auto *tab = m_traceInstances.value(instanceId);
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
                // 新建 Trace 实例（instanceId 为空时自动生成编号，非空时使用给定 ID）
                auto *newTab = new TraceTab(this);
                setupTraceTab(newTab);
                QString id = instanceId;
                if (id.isEmpty())
                    id = QString("trace%1").arg(++m_traceCount);
                else {
                    QRegularExpression re("trace(\\d+)", QRegularExpression::CaseInsensitiveOption);
                    auto m = re.match(id);
                    if (m.hasMatch()) {
                        int n = m.captured(1).toInt();
                        if (n > m_traceCount) m_traceCount = n;
                    }
                }
                QString numPart = id;
                numPart.remove("trace", Qt::CaseInsensitive);
                QString title = QString("Trace%1").arg(numPart.toInt());
                openTab(newTab, title);
                m_traceInstances[id] = newTab;
                view->addModuleInstance("trace", id, title);
                connect(newTab, &QObject::destroyed, this, [this, id](QObject *) {
                    m_traceInstances.remove(id);
                    // 延迟到下一轮事件循环，避免在析构链中同步修改场景导致崩溃
                    QMetaObject::invokeMethod(this, [this, id]() {
                        if (m_setupView)
                            m_setupView->removeModuleInstance("trace", id);
                    }, Qt::QueuedConnection);
                });
            }
        } else if (moduleId == "graphic") {
            auto *gv = m_graphicInstances.value(instanceId);
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
                // 新建 Graphic 实例
                auto *newGv = new GraphicView(this);
                QString id = instanceId;
                if (id.isEmpty())
                    id = QString("graphic%1").arg(++m_graphicCount);
                else {
                    QRegularExpression re("graphic(\\d+)", QRegularExpression::CaseInsensitiveOption);
                    auto m = re.match(id);
                    if (m.hasMatch()) {
                        int n = m.captured(1).toInt();
                        if (n > m_graphicCount) m_graphicCount = n;
                    }
                }
                QString numPart = id;
                numPart.remove("graphic", Qt::CaseInsensitive);
                QString title = QString("Graphic%1").arg(numPart.toInt());
                openTab(newGv, title);
                linkGraphicCursor(newGv);
                m_graphicInstances[id] = newGv;
                view->addModuleInstance("graphic", id, title);
                connect(newGv, &QObject::destroyed, this, [this, id](QObject *) {
                    m_graphicInstances.remove(id);
                    // 延迟到下一轮事件循环，避免在析构链中同步修改场景导致崩溃
                    QMetaObject::invokeMethod(this, [this, id]() {
                        if (m_setupView)
                            m_setupView->removeModuleInstance("graphic", id);
                    }, Qt::QueuedConnection);
                });
            }
        } else if (moduleId == "record") {
            onOpenRecordTab();
        } else if (moduleId == "data") {
            m_bottomPanel->appendOutput("Data 统计模块（待实现）");
        }
    });

    // 关闭模块实例请求
    // 使用 QTimer::singleShot(0) 延迟到下一轮事件循环，避免在右键菜单 exec() 的
    // 本地事件循环中触发 deleteLater() → destroyed → removeModuleInstance → rebuildScene()
    // 导致场景重建在 mousePressEvent 调用栈中执行而崩溃
    connect(view, &MeasurementSetupView::moduleInstanceClosed,
            this, [this](const QString &moduleId, const QString &instanceId) {
        QTimer::singleShot(0, this, [this, moduleId, instanceId]() {
            if (moduleId == "trace") {
                auto *tab = m_traceInstances.value(instanceId);
                if (tab) {
                    const auto allTabs = m_editorArea->allTabWidgets();
                    for (auto *tw : allTabs) {
                        int idx = tw->indexOf(tab);
                        if (idx >= 0) {
                            m_editorArea->closeTab(tw, idx);  // 同步关闭标签页 + 刷新侧边栏
                            break;
                        }
                    }
                }
            } else if (moduleId == "graphic") {
                auto *gv = m_graphicInstances.value(instanceId);
                if (gv) {
                    const auto allTabs = m_editorArea->allTabWidgets();
                    for (auto *tw : allTabs) {
                        int idx = tw->indexOf(gv);
                        if (idx >= 0) {
                            m_editorArea->closeTab(tw, idx);
                            break;
                        }
                    }
                }
            }
        });
    });

    // DBC 选择请求 → 打开 DBC 导入对话框
    connect(view, &MeasurementSetupView::dbcSelectRequested,
            this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "导入 DBC 文件", {}, "DBC 文件 (*.dbc);;所有文件 (*.*)");
        if (!path.isEmpty()) {
            if (m_dbcManager->loadDbc(path))
                m_bottomPanel->appendOutput("已加载 DBC: " + QFileInfo(path).fileName());
            else
                m_bottomPanel->addProblem(1, "DBC", "加载失败: " + path);
        }
    });

    // DBC 卸载请求 → 通过 DbcManager 卸载 + 关闭关联标签页
    connect(view, &MeasurementSetupView::dbcRemoveRequested,
            this, [this](const QString &fileName) {
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
    });

    // 通道过滤请求 → 输出到底部面板
    connect(view, &MeasurementSetupView::channelFilterRequested,
            this, [this](const QString &channelId) {
        m_bottomPanel->appendOutput(QString("通道 %1 过滤条件已配置").arg(channelId));
    });

    // 先打开 Flow 标签页，确保标签页顺序为 Flow → Trace1 → Graphic1
    openTab(view, "Flow");

    // 注册默认 Trace 实例到 flow 画布
    if (m_traceTab) {
        if (!m_traceInstances.contains("trace1")) {
            m_traceInstances["trace1"] = m_traceTab;
            connect(m_traceTab, &QObject::destroyed, this, [this](QObject *) {
                m_traceInstances.remove("trace1");
                m_traceTab = nullptr;
                QMetaObject::invokeMethod(this, [this]() {
                    if (m_setupView)
                        m_setupView->removeModuleInstance("trace", "trace1");
                }, Qt::QueuedConnection);
            });
        }
        view->addModuleInstance("trace", "trace1", "Trace1");
    } else if (!m_traceInstances.contains("trace1")) {
        // 首次启动 — 创建默认 Trace1 标签页
        auto *tab = new TraceTab(this);
        setupTraceTab(tab);
        if (m_filterPresets) {
            auto *filterBar = tab->filterBar();
            if (filterBar)
                filterBar->setPresetManager(m_filterPresets);
        }
        openTab(tab, "Trace1");
        m_traceCount = qMax(m_traceCount, 1);
        m_traceTab = tab;
        m_traceInstances["trace1"] = tab;
        view->addModuleInstance("trace", "trace1", "Trace1");
        connect(tab, &QObject::destroyed, this, [this](QObject *) {
            m_traceInstances.remove("trace1");
            m_traceTab = nullptr;
            QMetaObject::invokeMethod(this, [this]() {
                if (m_setupView)
                    m_setupView->removeModuleInstance("trace", "trace1");
            }, Qt::QueuedConnection);
        });
    }

    // 注册默认 Graphic 实例到 flow 画布
    if (m_graphicView) {
        if (!m_graphicInstances.contains("graphic1")) {
            m_graphicInstances["graphic1"] = m_graphicView;
            connect(m_graphicView, &QObject::destroyed, this, [this](QObject *) {
                m_graphicInstances.remove("graphic1");
                m_graphicView = nullptr;
                QMetaObject::invokeMethod(this, [this]() {
                    if (m_setupView)
                        m_setupView->removeModuleInstance("graphic", "graphic1");
                }, Qt::QueuedConnection);
            });
        }
        view->addModuleInstance("graphic", "graphic1", "Graphic1");
    } else if (!m_graphicInstances.contains("graphic1")) {
        // 首次启动 — 创建默认 Graphic1 标签页
        auto *gv = new GraphicView(this);
        openTab(gv, "Graphic1");
        linkGraphicCursor(gv);
        m_graphicCount = qMax(m_graphicCount, 1);
        m_graphicView = gv;
        m_graphicInstances["graphic1"] = gv;
        view->addModuleInstance("graphic", "graphic1", "Graphic1");
        m_sideBar->graphicConfigPanel()->setGraphicView(gv);
        connect(gv, &QObject::destroyed, this, [this](QObject *) {
            m_graphicInstances.remove("graphic1");
            m_graphicView = nullptr;
            QMetaObject::invokeMethod(this, [this]() {
                if (m_setupView)
                    m_setupView->removeModuleInstance("graphic", "graphic1");
            }, Qt::QueuedConnection);
        });
    }
}

// ============================================================
//  P0/P1 新增功能实现
// ============================================================

void MainWindow::onOpenDataWindow()
{
    if (!m_dataWindow) {
        m_dataWindow = new DataWindow(this);
        m_dataWindow->setDbcManager(m_dbcManager);
    }
    openTab(m_dataWindow, QStringLiteral("Data Window"));
}

void MainWindow::onOpenIOGraph()
{
    if (!m_ioGraph) {
        m_ioGraph = new IOGraphView(this);
    }
    openTab(m_ioGraph, QStringLiteral("I/O Graph"));
}

void MainWindow::onOpenColorRuleEditor()
{
    ColorRuleEditor dlg(this);
    // 加载当前规则 — CanTraceModel::ColorRule → ColorRuleEditor::ColorRule
    if (m_traceTab) {
        auto *model = m_traceTab->traceModel();
        if (model) {
            QVector<ColorRuleEditor::ColorRule> editorRules;
            for (const auto &r : model->colorRules()) {
                ColorRuleEditor::ColorRule er;
                er.expr = r.expr;
                er.background = r.background;
                er.foreground = r.foreground;
                er.enabled = r.enabled;
                editorRules.append(er);
            }
            dlg.setRules(editorRules);
        }
    }

    if (dlg.exec() == QDialog::Accepted) {
        auto rules = dlg.rules();
        // 应用到所有 Trace 标签页 — ColorRuleEditor::ColorRule → CanTraceModel::ColorRule
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
                if (tt) {
                    auto *model = tt->traceModel();
                    if (model) {
                        QVector<CanTraceModel::ColorRule> modelRules;
                        for (const auto &r : rules) {
                            CanTraceModel::ColorRule mr;
                            mr.expr = r.expr;
                            mr.background = r.background;
                            mr.foreground = r.foreground;
                            mr.enabled = r.enabled;
                            modelRules.append(mr);
                        }
                        model->setColorRules(modelRules);
                    }
                }
            }
        }
    }
}

void MainWindow::onBookmarkJumped(int frameIndex)
{
    // 跳转到指定帧
    auto *traceTab = qobject_cast<TraceTab *>(m_editorArea->currentWidget());
    if (traceTab) {
        auto *view = traceTab->traceView();
        if (view) {
            QModelIndex idx = view->model()->index(frameIndex, 0);
            if (idx.isValid()) {
                view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                view->selectRow(frameIndex);
            }
        }
    }
}

void MainWindow::onTriggerRecording(
    const QString &dir, const QString &prefix, const QString &format,
    bool splitBySize, int sizeMb, bool splitByTime, int timeSec,
    bool ringMode, int maxFiles,
    const QString &triggerExpr, double preTriggerSec, double postTriggerSec,
    bool repeatTrigger)
{
    if (!m_triggerRecorder) {
        m_triggerRecorder = new TriggerRecorder(this);
        connect(m_simulator, &CanSimulator::frameGenerated,
                m_triggerRecorder, &TriggerRecorder::onFrame);
        connect(m_deviceManager, &CanDeviceManager::frameGenerated,
                m_triggerRecorder, &TriggerRecorder::onFrame);
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
            QMessageBox::warning(this, "触发录制",
                "触发条件表达式编译失败，请检查表达式语法。");
            return;
        }
        m_statusLabel->setText("触发录制中... 等待触发条件");
    } else {
        m_triggerRecorder->stop();
        m_statusLabel->setText("触发录制已停止");
    }
}

void MainWindow::onGraphicPageSelected(int row)
{
    const auto allTabs = m_editorArea->allTabWidgets();
    int graphicIdx = 0;
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            if (tw->tabText(i).contains("Graphic")) {
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
    Q_UNUSED(section)
    SettingsDialog dlg(this);
    dlg.exec();
}

void MainWindow::refreshPanelLists()
{
    QStringList traceNames, graphicNames;
    const auto allTabs = m_editorArea->allTabWidgets();
    for (auto *tw : allTabs) {
        for (int i = 0; i < tw->count(); ++i) {
            QString name = tw->tabText(i);
            if (name.contains("Trace"))
                traceNames << name;
            if (name.contains("Graphic"))
                graphicNames << name;
        }
    }
    m_sideBar->tracePanel()->refreshList(traceNames);
    m_sideBar->graphicConfigPanel()->refreshList(graphicNames);
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
    // 硬件设备优先连接，模拟器作为备选
    if (m_deviceManager->isRealDevice() && !m_deviceManager->isRunning()) {
        m_deviceManager->start();
    } else if (!m_simulator->isRunning() && !m_deviceManager->isRealDevice()) {
        m_simulator->start();
        m_connLabel->setText("🔗 已连接");
        m_bottomPanel->appendOutput("设备已连接 (模拟器)");
    }
}

void MainWindow::onQuickDisconnect()
{
    m_simulator->stop();
    m_deviceManager->stop();
    m_connLabel->setText("🔗 未连接");
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
        auto *active = qobject_cast<TraceTab *>(m_editorArea->currentWidget());
        if (active) active->setFilterExpression(expr);
        out->appendTerminal("过滤: " + expr);
    } else if (cmd == "stats") {
        updateStatistics();
        auto *active = qobject_cast<TraceTab *>(m_editorArea->currentWidget());
        int total = active ? active->frameCount() : 0;
        out->appendTerminal(QString("总帧数: %1").arg(total));
    } else if (cmd.startsWith("load ")) {
        QString path = cmd.mid(5).trimmed();
        QFileInfo fi(path);
        if (fi.suffix().toLower() == "dbc") {
            m_dbcManager->loadDbc(path);
        } else if (CanFileIOFactory::canRead(fi.suffix())) {
            if (m_player->load(path)) {
                // 清除所有 Trace 数据
                const auto allTabs = m_editorArea->allTabWidgets();
                for (auto *tw : allTabs) {
                    for (int i = 0; i < tw->count(); ++i) {
                        auto *tt = qobject_cast<TraceTab *>(tw->widget(i));
                        if (tt) tt->clearTrace();
                    }
                }
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
    // 非测量状态：显示当前 Trace 标签页统计
    auto *active = qobject_cast<TraceTab *>(m_editorArea->currentWidget());
    int total = active ? active->frameCount() : 0;
    m_frameCountLabel->setText(QString::number(total) + " 帧");
    m_rowCountLabel->setText(QString::number(total) + "行");
    m_filterLabel->setText(QString("过滤%1/%2").arg(total).arg(total));
}

void MainWindow::updateActions()
{
    bool hasFile = m_player->isLoaded();
    bool playing = m_player->isPlaying();
    m_playAction->setEnabled(hasFile && !playing);
    m_pauseAction->setEnabled(playing);
    m_stopAction->setEnabled(hasFile);
    m_recordAction->setChecked(m_recording);
    if (m_playbackTab) m_playbackTab->setPlayerLoaded(hasFile, playing);
}

// ============================================================
//  视图菜单
// ============================================================

void MainWindow::toggleLeftDock()
{
    m_leftDock->setVisible(!m_leftDock->isVisible());
}

void MainWindow::toggleRightDock()
{
    m_rightDock->setVisible(!m_rightDock->isVisible());
}

void MainWindow::toggleBottomDock()
{
    m_bottomDock->setVisible(!m_bottomDock->isVisible());
}

void MainWindow::resetLayout()
{
    m_leftDock->setVisible(true);
    m_rightDock->setVisible(true);
    m_bottomDock->setVisible(true);
    resizeDocks({m_leftDock}, {280}, Qt::Horizontal);
    resizeDocks({m_rightDock}, {260}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {200}, Qt::Vertical);
}

// ============================================================
//  帮助菜单对话框
// ============================================================

void MainWindow::showAboutDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("关于 openbus");
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel("<b style='font-size:24px;color:#4a90d9'>openbus</b>", &dlg);
    auto *desc = new QLabel("CAN/CAN FD 报文分析工具", &dlg);
    auto *ver = new QLabel("版本: 1.0.0", &dlg);
    auto *author = new QLabel("作者: 蔡可杰 (Jake.cai)", &dlg);
    auto *github = new QLabel("Gitee: <a href='https://gitee.com/jake_cai/openbus'>https://gitee.com/jake_cai/openbus</a>", &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *email = new QLabel("邮箱: 929168503@qq.com", &dlg);
    auto *wechat = new QLabel("微信: 13368295840", &dlg);
    auto *biz = new QLabel("商业合作: 929168503@qq.com / 微信 13368295840", &dlg);
    biz->setObjectName("DimLabel");
    auto *copyright = new QLabel("基于 Qt6 构建 © 2026", &dlg);
    copyright->setObjectName("DimLabel");

    for (auto *l : {title, desc, ver, author, github, email, wechat, biz, copyright}) {
        layout->addWidget(l);
    }
    layout->addStretch();

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showLicenseDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("许可证");
    dlg.resize(500, 400);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(
        "MIT License\n\n"
        "Copyright (c) 2026 蔡可杰 (Jake.cai)\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy "
        "of this software and associated documentation files (the \"Software\"), to deal "
        "in the Software without restriction, including without limitation the rights "
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
        "copies of the Software, and to permit persons to whom the Software is "
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in all "
        "copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
        "SOFTWARE.");
    layout->addWidget(browser);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showReleaseNotes()
{
    QDialog dlg(this);
    dlg.setWindowTitle("发版记录");
    dlg.resize(500, 400);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(
        "v1.0.0 (2026-07-27)\n"
        "  首个正式版本\n"
        "  - CAN/CAN FD 报文实时采集与离线回放\n"
        "  - DBC 文件加载与信号级解析\n"
        "  - Wireshark 风格三栏 Trace 视图\n"
        "  - 多 Graphic 信号波形图（多纵轴）\n"
        "  - VS Code 风格可拆分标签页布局\n"
        "  - AI 对话助手集成\n\n"
        "v0.9.0 (2026-07-20)\n"
        "  Beta 预览版\n"
        "  - 无边框窗口 + 菜单栏拖拽\n"
        "  - ActivityBar + SideBar 多面板\n"
        "  - 基础报文录制与回放\n");
    layout->addWidget(browser);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showShortcuts()
{
    QDialog dlg(this);
    dlg.setWindowTitle("快捷键");
    dlg.resize(400, 350);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setPlainText(
        "播放 / 暂停      Space\n"
        "停止             Ctrl+S\n"
        "录制             Ctrl+R\n"
        "打开文件         Ctrl+O\n"
        "打开工程         Ctrl+Shift+O\n"
        "清空 Trace       Ctrl+L\n"
        "切换左侧栏       Ctrl+B\n"
        "切换底部栏       Ctrl+J\n"
        "向右拆分         Ctrl+\\\n"
        "关闭拆分组       Ctrl+W\n");
    browser->setObjectName("TerminalOutput");
    layout->addWidget(browser);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

void MainWindow::showCheckUpdate()
{
    QMessageBox::information(this, "检查更新",
        "当前版本: 1.0.0\n"
        "最新版本: 1.0.0 (已是最新)\n\n"
        "如有更新，请前往 Gitee Releases 页面下载最新版本。");
}

void MainWindow::showBusinessCoop()
{
    QDialog dlg(this);
    dlg.setWindowTitle("商业合作");
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel("<b style='color:#4a90d9'>如需商业授权、定制开发、技术支持或业务合作</b>", &dlg);
    title->setWordWrap(true);
    auto *author = new QLabel("作者: 蔡可杰 (Jake.cai)", &dlg);
    auto *email = new QLabel("邮箱: 929168503@qq.com", &dlg);
    auto *wechat = new QLabel("微信: 13368295840", &dlg);
    auto *github = new QLabel("Gitee: <a href='https://gitee.com/jake_cai/openbus'>https://gitee.com/jake_cai/openbus</a>", &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *note = new QLabel("本项目基于 MIT License 开源，商业使用请联系作者获取授权。", &dlg);
    note->setObjectName("DimLabel");
    note->setWordWrap(true);

    for (auto *l : {title, author, email, wechat, github, note}) {
        layout->addWidget(l);
    }
    layout->addStretch();

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    dlg.exec();
}

// ============================================================
//  事件过滤器
// ============================================================

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == menuBar()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                QAction *act = menuBar()->actionAt(me->pos());
                if (!act) {
                    if (windowHandle())
                        windowHandle()->startSystemMove();
                }
            }
        } else if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent *>(event);
            QAction *act = menuBar()->actionAt(me->pos());
            if (!act) {
                if (isMaximized())
                    showNormal();
                else
                    showMaximized();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// ============================================================
//  Windows 原生事件
// ============================================================

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            const int borderWidth = 5;
            RECT winrect;
            GetWindowRect(msg->hwnd, &winrect);
            long x = GET_X_LPARAM(msg->lParam);
            long y = GET_Y_LPARAM(msg->lParam);

            bool left   = x >= winrect.left && x < winrect.left + borderWidth;
            bool right  = x < winrect.right && x >= winrect.right - borderWidth;
            bool top    = y >= winrect.top && y < winrect.top + borderWidth;
            bool bottom = y < winrect.bottom && y >= winrect.bottom - borderWidth;

            if (top && left)     { *result = HTTOPLEFT;     return true; }
            if (top && right)    { *result = HTTOPRIGHT;    return true; }
            if (bottom && left)  { *result = HTBOTTOMLEFT;  return true; }
            if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
            if (left)            { *result = HTLEFT;         return true; }
            if (right)           { *result = HTRIGHT;        return true; }
            if (top)             { *result = HTTOP;          return true; }
            if (bottom)          { *result = HTBOTTOM;       return true; }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

// ============================================================
//  插件系统集成
// ============================================================

void MainWindow::refreshPluginList()
{
    if (!m_sideBar || !m_sideBar->extensionsPanel() || !m_pluginManager)
        return;

    QList<ExtensionEntry> entries;
    const auto plugins = m_pluginManager->discoveredPlugins();
    for (const auto &info : plugins) {
        ExtensionEntry e;
        e.name = info.name;
        e.version = info.version;
        e.author = info.author;
        e.description = info.description;
        e.iconPath = info.iconFilePath();
        e.installed = true;
        e.enabled = m_pluginManager->isPluginEnabled(info.name);
        e.activated = m_pluginManager->isPluginActivated(info.name);
        entries.append(e);
    }
    m_sideBar->extensionsPanel()->refreshInstalledPlugins(entries);
}

void MainWindow::onPluginOutput(const QString &text)
{
    m_bottomPanel->appendPluginOutput(text);
}

void MainWindow::onPluginCommandRegistered(const QString &id, const QString &title)
{
    // 添加到侧边栏扩展面板的命令列表
    if (m_sideBar && m_sideBar->extensionsPanel())
        m_sideBar->extensionsPanel()->addCommand(id, title);
}

void MainWindow::onPluginSendFrame(const CanFrame &frame)
{
    if (m_deviceManager)
        m_deviceManager->sendFrame(frame);
}

void MainWindow::onPluginRequestSelectedFrames(const QJsonValue &requestId)
{
    QList<CanFrame> frames;

    // 获取当前活跃 TraceTab 中选中的帧
    auto *active = qobject_cast<TraceTab *>(m_editorArea->currentWidget());
    if (active) {
        auto *tv = active->traceView();
        auto *traceModel = active->traceModel();
        auto *filterProxy = active->proxyModel();
        auto *viewportProxy = active->viewportProxy();
        if (tv && traceModel) {
            auto *sel = tv->selectionModel();
            if (sel) {
                for (const auto &idx : sel->selectedRows()) {
                    // 代理链：View → ViewportProxy → CanFilterProxy → CanTraceModel
                    QModelIndex sourceIdx = idx;
                    if (viewportProxy)
                        sourceIdx = viewportProxy->mapToSource(sourceIdx);
                    if (filterProxy)
                        sourceIdx = filterProxy->mapToSource(sourceIdx);
                    int row = sourceIdx.row();
                    if (row >= 0 && row < traceModel->frameCount())
                        frames.append(traceModel->frameAt(row));
                }
            }
        }
    }

    if (m_pluginManager)
        m_pluginManager->provideSelectedFrames(requestId, frames);
}

// ============================================================
//  关闭
// ============================================================

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_recording) {
        if (QMessageBox::question(this, "退出", "正在录制，确定退出？") != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_recorder->stop();
    }
    m_simulator->stop();
    m_deviceManager->stop();
    m_player->stop();

    // 关闭插件系统
    if (m_pluginManager)
        m_pluginManager->shutdown();

    // 自动保存工程
    if (AppConfig::instance()->getBool("project.autoSaveOnClose", true)) {
        captureProjectState();
        if (!ProjectManager::instance()->currentFilePath().isEmpty()) {
            ProjectManager::instance()->saveProject();
        }
    }

    event->accept();
}

// ============================================================
//  工程管理
// ============================================================

void MainWindow::onOpenProject()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开工程"), {},
        QStringLiteral("openbus 工程文件 (*.openbusproj);;所有文件 (*.*)"));
    if (path.isEmpty()) return;

    // 先保存当前工程状态
    captureProjectState();
    if (!ProjectManager::instance()->currentFilePath().isEmpty())
        ProjectManager::instance()->saveProject();

    // 加载新工程
    if (ProjectManager::instance()->loadProject(path)) {
        applyProjectState();
        m_bottomPanel->appendOutput(QStringLiteral("工程已加载: ") +
                                    ProjectManager::instance()->currentProjectName());
    }
}

void MainWindow::onSaveProject()
{
    captureProjectState();

    QString path = ProjectManager::instance()->currentFilePath();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存工程"),
            ProjectManager::instance()->currentProjectName() + ".openbusproj",
            QStringLiteral("openbus 工程文件 (*.openbusproj);;所有文件 (*.*)"));
        if (path.isEmpty()) return;
    }

    if (ProjectManager::instance()->saveProject(path)) {
        m_bottomPanel->appendOutput(QStringLiteral("工程已保存: ") + path);
    }
}

void MainWindow::onProjectSwitched(int index)
{
    auto &projects = m_sideBar->projectPanel()->projectsRef();
    if (index < 0 || index >= projects.size()) return;

    // 1. 保存当前工程状态
    int curIdx = m_sideBar->projectPanel()->currentIndex();
    captureProjectState();
    if (curIdx >= 0 && curIdx < projects.size()) {
        // 将状态快照保存到 ProjectContext，未保存的工程切换回来时可恢复
        projects[curIdx].stateJson = ProjectManager::instance()->toJsonString();
    }
    if (!ProjectManager::instance()->currentFilePath().isEmpty())
        ProjectManager::instance()->saveProject();

    // 2. 恢复目标工程状态
    const auto &target = projects.at(index);
    if (!target.filePath.isEmpty() && QFile::exists(target.filePath)) {
        // 已保存到文件的工程：从文件加载
        if (ProjectManager::instance()->loadProject(target.filePath)) {
            applyProjectState();
            m_bottomPanel->appendOutput(QStringLiteral("已切换到工程: ") +
                                        ProjectManager::instance()->currentProjectName());
        }
    } else if (!target.stateJson.isEmpty()) {
        // 未保存但有状态快照：从快照恢复
        ProjectManager::instance()->fromJsonString(target.stateJson);
        applyProjectState();
        m_bottomPanel->appendOutput(QStringLiteral("已切换到工程: ") +
                                    ProjectManager::instance()->currentProjectName());
    } else {
        // 全新工程：创建空状态
        ProjectManager::instance()->newProject(target.name);
        applyProjectState();
    }

    // 3. 刷新树形列表，更新 "(当前)" 标记和文件子节点
    projects[index].stateJson = ProjectManager::instance()->toJsonString();
    m_sideBar->projectPanel()->refreshList();
}

void MainWindow::onProjectCreated(const QString &name)
{
    // 1. 保存当前工程状态到状态快照
    auto &projects = m_sideBar->projectPanel()->projectsRef();
    int curIdx = m_sideBar->projectPanel()->currentIndex();
    captureProjectState();
    if (curIdx >= 0 && curIdx < projects.size())
        projects[curIdx].stateJson = ProjectManager::instance()->toJsonString();
    if (!ProjectManager::instance()->currentFilePath().isEmpty())
        ProjectManager::instance()->saveProject();

    // 2. 创建新工程
    ProjectManager::instance()->newProject(name);
    applyProjectState();
    m_bottomPanel->appendOutput(QStringLiteral("已创建新工程: ") + name);

    // 3. 刷新树形列表
    projects[curIdx].stateJson = ProjectManager::instance()->toJsonString();
    m_sideBar->projectPanel()->refreshList();
}

void MainWindow::captureProjectState()
{
    auto &st = ProjectManager::instance()->currentStateRef();

    // 数据源
    if (m_setupView) {
        st.sourceMode = static_cast<int>(m_setupView->currentSource());
        st.filePath = m_setupView->filePath();
    }

    // 波特率 / 通道
    if (m_simulator) {
        st.baudrate = m_simulator->baudrate();
        st.channel = m_simulator->channel();
    }

    // 设备配置（CAN FD / 数据段波特率 / 设备类型）
    if (m_deviceTab) {
        st.deviceConfig.fd = m_deviceTab->isCanFd();
        st.deviceConfig.fdBaudrate = m_deviceTab->dataBaudrate();
        st.deviceConfig.type = QStringLiteral("devKind%1").arg(m_deviceTab->deviceKind());
        // 覆盖 simulator 值——DeviceConnectionTab 是用户实际配置的来源
        if (m_deviceTab->baudrate() > 0)
            st.baudrate = m_deviceTab->baudrate();
        if (m_deviceTab->channel() > 0)
            st.channel = m_deviceTab->channel();
    }

    // DBC 文件
    st.dbcFiles.clear();
    if (m_dbcManager) {
        for (const auto &f : m_dbcManager->files())
            st.dbcFiles << f.filePath;
    }

    // Trace 实例 — 只保存 trace1 (默认实例)
    st.traces.clear();
    {
        auto it = m_traceInstances.find("trace1");
        if (it != m_traceInstances.end()) {
            ProjectTraceInstance ti;
            ti.id = "trace1";
            ti.title = "Trace1";
            auto *tab = qobject_cast<TraceTab*>(it.value());
            if (tab)
                ti.filterExpression = tab->filterExpression();
            st.traces.append(ti);
        }
    }

    // Graphic 实例 — 只保存 graphic1 (默认实例)
    st.graphics.clear();
    {
        auto it = m_graphicInstances.find("graphic1");
        if (it != m_graphicInstances.end()) {
            ProjectGraphicInstance gi;
            gi.id = "graphic1";
            gi.title = "Graphic1";
            auto *gv = qobject_cast<GraphicView*>(it.value());
            if (gv) {
                for (const auto &sig : gv->signalConfigs()) {
                    ProjectSigCfg sc;
                    sc.canId = sig.canId;
                    sc.name = sig.name;
                    sc.extended = sig.extended;
                    gi.sigList.append(sc);
                }
            }
            st.graphics.append(gi);
        }
    }

    // 标签页顺序
    st.openTabs.clear();
    st.activeTab.clear();
    if (m_editorArea) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i)
                st.openTabs << tw->tabText(i).trimmed();
            int idx = tw->currentIndex();
            if (idx >= 0 && idx < tw->count())
                st.activeTab = tw->tabText(idx).trimmed();
            break;
        }
    }

    ProjectManager::instance()->currentStateRef() = st;
}

void MainWindow::applyProjectState()
{
    const auto &st = ProjectManager::instance()->currentState();

    // 1. 停止测量
    m_simulator->stop();
    m_player->stop();

    // 2. 关闭当前所有 Trace/Graphic 标签页
    //    使用 delete 而非 deleteLater — 必须在 DBC 卸载前销毁 widget，
    //    防止旧 TraceTab/GraphicView 在 DBC 卸载后访问已释放的 DBC 数据
    if (m_editorArea) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = tw->count() - 1; i >= 0; --i) {
                QString text = tw->tabText(i);
                if (text.contains("Trace") || text.contains("Graphic")) {
                    QWidget *w = tw->widget(i);
                    tw->removeTab(i);
                    delete w;  // 立即销毁，防止 use-after-free
                }
            }
        }
    }
    m_traceInstances.clear();
    m_graphicInstances.clear();
    m_traceCount = 0;
    m_graphicCount = 0;
    m_traceTab = nullptr;
    m_graphicView = nullptr;

    // 3. 卸载所有 DBC 并重新加载
    auto dbcFiles = m_dbcManager->files();
    for (const auto &f : dbcFiles)
        m_dbcManager->unloadDbc(f.filePath);
    for (const auto &path : st.dbcFiles) {
        if (QFile::exists(path)) {
            m_dbcManager->loadDbc(path);
        } else {
            m_bottomPanel->appendOutput(QStringLiteral("DBC 文件不存在: ") + path);
        }
    }

    // 4. 设置数据源
    if (m_setupView) {
        m_setupView->setSource(static_cast<MeasurementSetupView::Source>(st.sourceMode));
        m_setupView->setFilePath(st.filePath);
    }

    // 5. 波特率 / 通道 / 设备配置
    m_simulator->setChannel(static_cast<quint8>(st.channel));
    m_simulator->setBaudrate(st.baudrate);
    // 恢复 DeviceConnectionTab 界面配置（不连接设备，仅恢复参数）
    if (m_deviceTab) {
        m_deviceTab->setBaudrate(st.baudrate);
        m_deviceTab->setChannel(st.channel);
        m_deviceTab->setCanFd(st.deviceConfig.fd);
        if (st.deviceConfig.fdBaudrate > 0)
            m_deviceTab->setDataBaudrate(st.deviceConfig.fdBaudrate);
    }

    // 6. 创建 Trace 实例 — 只恢复 trace1
    for (const auto &t : st.traces) {
        if (t.id != "trace1")
            continue;  // 忽略多余的 Trace 实例
        auto *tab = new TraceTab(this);
        setupTraceTab(tab);
        if (!t.filterExpression.isEmpty())
            tab->setFilterExpression(t.filterExpression);
        openTab(tab, "Trace1");
        m_traceInstances["trace1"] = tab;
        m_traceTab = tab;
        m_traceCount = qMax(m_traceCount, 1);
    }
    // 若保存状态中没有 trace1，则创建默认的
    if (!m_traceInstances.contains("trace1")) {
        auto *tab = new TraceTab(this);
        setupTraceTab(tab);
        openTab(tab, "Trace1");
        m_traceInstances["trace1"] = tab;
        m_traceTab = tab;
        m_traceCount = qMax(m_traceCount, 1);
    }

    // 7. 创建 Graphic 实例 — 只恢复 graphic1
    for (const auto &g : st.graphics) {
        if (g.id != "graphic1")
            continue;  // 忽略多余的 Graphic 实例
        auto *gv = new GraphicView(this);
        // 重建信号配置
        QVector<GraphicView::Signal> sigConfigs;
        for (const auto &s : g.sigList) {
            GraphicView::Signal sig;
            sig.name = s.name;
            sig.canId = s.canId;
            sig.extended = s.extended;
            // 从 DbcManager 查找完整的 DbcSignal 定义
            const DbcMessage *msg = m_dbcManager->findMessage(s.canId);
            if (msg) {
                const DbcSignal *ds = msg->findSignal(s.name);
                if (ds) sig.dbcSig = *ds;
            }
            sigConfigs.append(sig);
        }
        if (!sigConfigs.isEmpty())
            gv->loadSignalConfigs(sigConfigs);
        openTab(gv, "Graphic1");
        linkGraphicCursor(gv);
        m_graphicInstances["graphic1"] = gv;
        m_graphicView = gv;
        m_sideBar->graphicConfigPanel()->setGraphicView(gv);
        m_graphicCount = qMax(m_graphicCount, 1);
    }
    // 若保存状态中没有 graphic1，则创建默认的
    if (!m_graphicInstances.contains("graphic1")) {
        auto *gv = new GraphicView(this);
        openTab(gv, "Graphic1");
        linkGraphicCursor(gv);
        m_graphicInstances["graphic1"] = gv;
        m_graphicView = gv;
        m_sideBar->graphicConfigPanel()->setGraphicView(gv);
        m_graphicCount = qMax(m_graphicCount, 1);
    }

    // 8. 更新 flow 视图
    if (m_setupView) {
        // 先清除旧工程的 Trace/Graphic 实例块，再添加新工程的实例
        m_setupView->clearTraceGraphicInstances();
        for (const auto &t : st.traces)
            m_setupView->addModuleInstance("trace", t.id, t.title);
        for (const auto &g : st.graphics)
            m_setupView->addModuleInstance("graphic", g.id, g.title);
        m_setupView->rebuildScene();
    }

    // 9. 恢复活跃标签页
    if (m_editorArea && !st.activeTab.isEmpty()) {
        const auto allTabs = m_editorArea->allTabWidgets();
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                if (tw->tabText(i).trimmed() == st.activeTab) {
                    tw->setCurrentIndex(i);
                    if (m_tabLabel) m_tabLabel->setText(tw->tabText(i));
                    break;
                }
            }
        }
    }

    // 10. 更新窗口标题
    setWindowTitle(QStringLiteral("openbus - %1").arg(st.name));

    m_bottomPanel->appendOutput(QStringLiteral("工程现场已恢复: %1").arg(st.name));
}

// ============================================================
//  工程文件文本预览标签页
// ============================================================
void MainWindow::onFilePreviewRequested(const QString &filePath)
{
    QFileInfo fi(filePath);
    QString label = QStringLiteral("[预览] %1").arg(fi.fileName());

    // 检查是否已存在同名标签页
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == label) {
                tabs->setCurrentIndex(i);
                if (m_tabLabel) m_tabLabel->setText(label);
                return;
            }
        }
    }

    // 创建文本预览部件
    auto *preview = new QPlainTextEdit(this);
    preview->setReadOnly(true);
    preview->setFont(QFont(QStringLiteral("Consolas"), 10));

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        preview->setPlainText(QStringLiteral("无法打开文件:\n%1").arg(filePath));
    } else {
        QByteArray data = file.readAll();
        file.close();

        // 检测二进制内容（包含 NULL 字节）
        if (data.contains('\0') && data.size() > 0) {
            preview->setPlainText(
                QStringLiteral("二进制文件，无法以文本方式预览:\n%1\n\n文件大小: %2 bytes")
                    .arg(filePath)
                    .arg(data.size()));
        } else {
            QString ext = fi.suffix().toLower();
            if (ext == "openbusproj" || ext == "json") {
                // JSON 文件 — 格式化输出
                try {
                    auto j = nlohmann::json::parse(data.toStdString());
                    preview->setPlainText(QString::fromStdString(j.dump(2)));
                } catch (...) {
                    preview->setPlainText(QString::fromUtf8(data));
                }
            } else {
                preview->setPlainText(QString::fromUtf8(data));
            }
        }
    }

    openTab(preview, label);
}
