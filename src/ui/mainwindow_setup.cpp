// ============================================================
//  MainWindow 构造分阶段装配（B6 拆分自 mainwindow.cpp 构造函数）
//  各方法按原构造函数执行顺序被依次调用——顺序即语义，勿随意重排：
//    setupCoreServices → createXxx(UI) → connectExtensionsPanel →
//    connectDataPipeline → connectSidePanels → connectProjectPanel
// ============================================================
#include "mainwindow.h"
#include <QTimer>
#include "core/canframe.h"
#include "core/recorder.h"
#include "core/player.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "ui/activitybar.h"
#include "ui/panels/sidebarpanels.h"
#include "ui/thememanager.h"
#include "ui/bottompanel.h"
#include "ui/rightpanel.h"
#include "ui/spliteditorarea.h"
#include "core/driver/driverregistry.h"
#include "core/module/moduleregistry.h"
#include "core/module/imodule.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "core/marketmodel.h"   // MarketItem（ExtensionsPanel 信号类型，经 QVariant 传给市场模块）
#include "core/busstatistics.h"
#include "core/bookmarkmanager.h"
#include "core/appconfig.h"
#include "core/projectmanager.h"
#include "core/plugin/pluginmanager.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>

// ============================================================
//  阶段 1：数据层 / 核心服务 / 插件 / 驱动（须在 UI 构建前）
// ============================================================

void MainWindow::setupCoreServices()
{
    // ---- 数据层 ----
    m_dbcManager = new DbcManager(this);
    m_recorder  = new Recorder(this);
    m_player    = new Player(this);
    m_simulator = new CanSimulator(this);
    m_deviceManager = new CanDeviceManager(this);

    // ---- P0/P1 核心服务 ----
    m_busStats = new BusStatistics(this);
    // m_filterPresets 已随 Trace 页迁入 TraceModule（B5：模块自持 FilterPresetManager）
    m_bookmarkMgr = new BookmarkManager(this);

    // ---- 插件系统 ----
    // 注（DEF-08）：下列信号所属类定义于 libopenbus_data.dll，本文件编译于
    // openbus_ui（链入 exe）——MinGW 下 PMF 元方法解析失败，connect 静默断连。
    // 统一改用 SIGNAL() 字符串形式（运行期字符串匹配不走 PMF 解析），
    // 槽端保留新式写法。全库同类调用点均已同步改造。
    m_pluginManager = PluginManager::instance();
    // 注入工程 DBC 管理器（插件 signals.* 解码/编码用，纯消费者）
    m_pluginManager->setDbcManager(m_dbcManager);
    connect(m_pluginManager, SIGNAL(outputMessage(QString)),
            this, SLOT(onPluginOutput(QString)));
    connect(m_pluginManager, SIGNAL(commandRegistered(QString,QString)),
            this, SLOT(onPluginCommandRegistered(QString,QString)));
    connect(m_pluginManager, SIGNAL(sendFrameRequested(CanFrame)),
            this, SLOT(onPluginSendFrame(CanFrame)));
    connect(m_pluginManager, SIGNAL(requestSelectedFrames(QJsonValue)),
            this, SLOT(onPluginRequestSelectedFrames(QJsonValue)));
    connect(m_pluginManager, SIGNAL(requestRecentFrames(QJsonValue,int)),
            this, SLOT(onPluginRequestRecentFrames(QJsonValue,int)));
    // 注：outputClearRequested → BottomPanel::clearPluginOutput 在阶段 4
    // connectUiSignals() 中连接（此处 m_bottomPanel 尚未创建）
    m_pluginManager->initialize();

    // ---- 驱动系统 ----
    // 清理待卸载目录 + 扫描加载外置 .odp 驱动（须在 UI 构建前，设备树首建即完整）
    DriverRegistry::instance()->initialize();
}

// ============================================================
//  阶段 2：ActivityBar + 迷你市场面板 + 市场页创建
// ============================================================

void MainWindow::connectExtensionsPanel()
{
    // ActivityBar
    connect(m_activityBar, &ActivityBar::activityChanged,
            this, &MainWindow::onActivityChanged);
    connect(m_activityBar, &ActivityBar::activityToggled,
            this, &MainWindow::onActivityToggled);

    // ExtensionsPanel（迷你市场）→ 插件/驱动操作（方案 §13.10：与市场页同一语义）
    auto *extPanel = m_sideBar->extensionsPanel();
    if (extPanel) {
        connect(extPanel, &ExtensionsPanel::commandTriggered,
                this, [this](const QString &id) {
            if (m_pluginManager) m_pluginManager->executeCommand(id);
        });
        // 行点击 → 打开插件市场页并定位该条目详情
        connect(extPanel, &ExtensionsPanel::itemActivated,
                this, [this](const MarketItem &item) {
            onOpenMarketTab();
            marketInvoke(QStringLiteral("revealItem"), QVariant::fromValue(item));
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
        // 驱动禁用/卸载（与市场页详情按钮同一文案与语义）
        connect(extPanel, &ExtensionsPanel::driverToggleRequested,
                this, [this](const QString &driverId, bool enable) {
            DriverRegistry::instance()->setDriverEnabled(driverId, enable);
            // driversChanged → 市场页/侧边栏/设备树自动刷新
        });
        connect(extPanel, &ExtensionsPanel::driverUninstallRequested,
                this, [this](const QString &driverId) {
            if (QMessageBox::question(
                    this, QStringLiteral("卸载驱动"),
                    QStringLiteral("确定卸载驱动 %1？\n\n"
                                   "若其 DLL 已被本次运行加载，重启程序后将彻底清理（方案 §7.4）。")
                        .arg(driverId))
                != QMessageBox::Yes)
                return;
            const QString err = DriverRegistry::instance()->uninstallExternal(driverId);
            if (!err.isEmpty())
                QMessageBox::warning(this, QStringLiteral("卸载驱动"), err);
        });
        // 离线安装 .odp/.opk（与市场页「⋯ 安装」同一安装链）
        connect(extPanel, &ExtensionsPanel::installFromFileRequested,
                this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("选择驱动/插件包"), QString(),
                QStringLiteral("openbus 驱动与插件包 (*.odp *.opk);;所有文件 (*)"));
            if (path.isEmpty())
                return;
            if (!m_marketWidget)
                setupMarketTab();
            marketInvoke(QStringLiteral("installLocalFile"), path);
        });
        connect(extPanel, &ExtensionsPanel::openMarketRequested,
                this, &MainWindow::onOpenMarketTab);
    }

    // 统一插件市场标签页（见 setupMarketTab）
    setupMarketTab();

    // pluginListChanged → 刷新市场页已装列表（连接在 manager 上，标签页删除后仍安全）
    if (m_pluginManager) {
        auto *pluginRelay = new SignalRelay(this);
        pluginRelay->fire0 = [this]() {
            if (m_marketWidget) marketInvoke(QStringLiteral("refreshInstalled"));
        };
        connect(m_pluginManager, SIGNAL(pluginListChanged()),
                pluginRelay, SLOT(fire()));
    }
}

// ============================================================
//  阶段 3：模拟器 / 设备管理器 / 回放器 / 录制器 → 壳槽
// ============================================================

void MainWindow::connectDataPipeline()
{
    // 模拟器 → 帧接收（DEF-08 字符串信号）
    connect(m_simulator, SIGNAL(frameGenerated(CanFrame)),
            this, SLOT(onFrameReceived(CanFrame)));

    // 硬件设备管理器 → 帧接收（与模拟器同信号）
    connect(m_deviceManager, SIGNAL(frameGenerated(CanFrame)),
            this, SLOT(onFrameReceived(CanFrame)));
    auto *connRelay = new SignalRelay(this);
    connRelay->fnBoolString = [this](bool connected, const QString &name) {
        if (connected) {
            m_connLabel->setText(QStringLiteral("已连接: %1").arg(name));
            m_bottomPanel->appendOutput(QStringLiteral("硬件已连接: %1").arg(name));
            // 重连成功：清除 Flow 页数据源块错误标记（红闪熄灭）
            flowInvoke(QStringLiteral("setBlockError"),
                       QVariantList{ QStringLiteral("source_real"), false });
        } else {
            m_connLabel->setText("未连接");
            m_bottomPanel->appendOutput(QStringLiteral("硬件已断开"));
        }
    };
    connect(m_deviceManager, SIGNAL(connectionChanged(bool,QString)),
            connRelay, SLOT(fireBoolQString(bool,QString)));
    auto *errRelay = new SignalRelay(this);
    errRelay->fnString = [this](const QString &msg) {
        m_bottomPanel->appendOutput(QStringLiteral("%1").arg(msg));
        // 设备错误 → Flow 页数据源块红灯闪烁（运行异常可视化）
        flowInvoke(QStringLiteral("setBlockError"),
                   QVariantList{ QStringLiteral("source_real"), true });
    };
    connect(m_deviceManager, SIGNAL(errorOccurred(QString)),
            errRelay, SLOT(fireQString(QString)));

    // 回放器
    connect(m_player, SIGNAL(framePlayed(CanFrame)), this, SLOT(onFramePlayed(CanFrame)));
    connect(m_player, SIGNAL(progressChanged(int,int,double,double)), this, SLOT(onPlayerProgress(int,int,double,double)));
    connect(m_player, SIGNAL(stateChanged(bool)), this, SLOT(onPlayerStateChanged(bool)));
    connect(m_player, SIGNAL(finished()), this, SLOT(onPlayerFinished()));

    // 录制器
    auto *recStartRelay = new SignalRelay(this);
    recStartRelay->fire0 = [this]() {
        m_recording = true;
        m_bottomPanel->appendOutput("录制开始");
        transceiveInvoke(QStringLiteral("setRecording"), true);
        updateActions();
    };
    connect(m_recorder, SIGNAL(recordingStarted(QString)), recStartRelay, SLOT(fire()));
    auto *recStopRelay = new SignalRelay(this);
    recStopRelay->fnStringInt = [this](const QString &path, int count) {
        m_recording = false;
        m_bottomPanel->appendOutput(QString("录制结束: %1 (%2 帧)").arg(path).arg(count));
        transceiveInvoke(QStringLiteral("setRecording"), false);
        // 记入工程状态（工程树"录制文件"节点数据来源）
        auto &st = ProjectManager::instance()->currentStateRef();
        if (!st.recordFiles.contains(path))
            st.recordFiles << path;
        updateActions();
    };
    connect(m_recorder, SIGNAL(recordingStopped(QString,int)),
            recStopRelay, SLOT(fireQStringInt(QString,int)));
}

// ============================================================
//  阶段 4：侧边栏 / 右侧面板 / 编辑区 / 底部面板接线
// ============================================================

void MainWindow::connectSidePanels()
{
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
                if (isTraceTabText(tw->tabText(i))) {
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
    // 「添加信号」→ DBC 信号选择弹窗（搜索 / 树形 / Ctrl+Shift 多选 → addSignals）
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::addSignalRequested,
            this, &MainWindow::onGraphicAddSignalRequested);
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::graphicPageSelected,
            this, &MainWindow::onGraphicPageSelected);
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::graphicDeleteRequested,
            this, [this](int row) {
        // 侧边栏删除 Graphic → 找到对应标签页并关闭（触发完整清理链）
        const auto allTabs = m_editorArea->allTabWidgets();
        int graphicIdx = 0;
        for (auto *tw : allTabs) {
            for (int i = 0; i < tw->count(); ++i) {
                if (isGraphicTabText(tw->tabText(i))) {
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
    // 主题切换后刷新 ActivityBar 图标颜色（DEF-08 字符串信号）
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            m_activityBar, SLOT(refreshIcons()));
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
    // setGraphicView 已随 B5-5 移除 — Graphic 实例编排经 graphic 模块接口
    m_sideBar->devicePanel()->setSimulator(m_simulator);
    m_sideBar->devicePanel()->setDeviceManager(m_deviceManager);

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
            else if (isTraceTabText(text))
                act = ActivityBar::Trace;
            else if (isGraphicTabText(text))
                act = ActivityBar::Graphic;
            else if (text.contains("DBC"))
                act = ActivityBar::Dbc;
            else if (text.contains("发送") || text.contains("回放") ||
                     text.contains("录制") || text.contains("离线分析"))
                act = ActivityBar::Transceive;
            else if (text.contains(QStringLiteral("插件市场")))
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

    // 插件 output.clear（DEF-08 字符串信号，宿主跨 DLL）
    connect(m_pluginManager, SIGNAL(outputClearRequested()),
            m_bottomPanel, SLOT(clearPluginOutput()));

    // DBC 加载通知（DEF-08 字符串信号）
    auto *dbcLoadedRelay = new SignalRelay(this);
    dbcLoadedRelay->fnString = [this](const QString &name) {
        m_bottomPanel->appendOutput("DBC 已加载: " + name);
    };
    connect(m_dbcManager, SIGNAL(dbcLoaded(QString)),
            dbcLoadedRelay, SLOT(fireQString(QString)));
}

// ============================================================
//  阶段 5：工程面板 + 上次工程加载 + 默认实例兜底
// ============================================================

void MainWindow::connectProjectPanel()
{
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
    if (m_traceInstances.isEmpty())
        createTraceInstance(QStringLiteral("trace1"));
    if (m_graphicInstances.isEmpty())
        createGraphicInstance(QStringLiteral("graphic1"));
    flowInvoke(QStringLiteral("rebuildScene"), {});
}
