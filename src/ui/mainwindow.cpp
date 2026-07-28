#include "mainwindow.h"
#include "core/canframe.h"
#include "core/recorder.h"
#include "core/player.h"
#include "core/cansimulator.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "models/cantracemodel.h"
#include "models/canfilterproxymodel.h"
#include "ui/traceview.h"
#include "ui/graphicview.h"
#include "ui/filterbar.h"
#include "ui/activitybar.h"
#include "ui/panels/sidebarpanels.h"
#include "ui/bottompanel.h"
#include "ui/rightpanel.h"
#include "ui/spliteditorarea.h"
#include "ui/playbacktab.h"
#include "ui/recordtab.h"
#include "ui/dbcdetailtab.h"
#include "utils/canutils.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QStatusBar>
#include <QApplication>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <QToolButton>
#include <QMouseEvent>
#include <QWindow>
#include <QDesktopServices>
#include <QUrl>
#include <QLineEdit>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("sin - CAN/CAN FD 报文分析工具");
    resize(1400, 900);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

    // ---- 数据层 ----
    m_traceModel = new CanTraceModel(this);
    m_proxyModel = new CanFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_traceModel);
    m_dbcManager = new DbcManager(this);

    // ---- 核心引擎 ----
    m_recorder  = new Recorder(this);
    m_player    = new Player(this);
    m_simulator = new CanSimulator(this);

    // ---- UI 构建 ----
    createMenuBar();
    createWindowButtons();
    createLayout();
    createStatusBar();

    menuBar()->installEventFilter(this);

    // ---- 信号连接 ----

    // ActivityBar
    connect(m_activityBar, &ActivityBar::activityChanged,
            this, &MainWindow::onActivityChanged);
    connect(m_activityBar, &ActivityBar::activityToggled,
            this, &MainWindow::onActivityToggled);

    // 模拟器 → 帧接收
    connect(m_simulator, &CanSimulator::frameGenerated,
            this, &MainWindow::onFrameReceived);

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

    // 过滤栏
    auto *filterBar = m_traceTab->filterBar();
    connect(filterBar, &FilterBar::filterApplied, this, &MainWindow::onFilterApplied);
    connect(filterBar, &FilterBar::filterCleared, this, &MainWindow::onFilterCleared);

    // Trace 选择
    auto *traceView = m_traceTab->traceView();
    connect(traceView, &TraceView::frameDoubleClicked,
            this, &MainWindow::onFrameDoubleClicked);
    connect(traceView, &TraceView::frameSelected,
            this, &MainWindow::onTraceSelectionChanged);

    // PlaybackTab
    connect(m_playbackTab, &PlaybackTab::playRequested, this, &MainWindow::onPlay);
    connect(m_playbackTab, &PlaybackTab::pauseRequested, this, &MainWindow::onPause);
    connect(m_playbackTab, &PlaybackTab::stopRequested, this, &MainWindow::onStop);
    connect(m_playbackTab, &PlaybackTab::speedChanged, this, &MainWindow::onSpeedChanged);
    connect(m_playbackTab, &PlaybackTab::seekChanged, this, &MainWindow::onSeekChanged);
    connect(m_playbackTab, &PlaybackTab::changeFileRequested, this, &MainWindow::onOpenFile);

    // RecordTab
    connect(m_recordTab, &RecordTab::recordToggled, this, [this](bool on) {
        if (on) {
            QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".sin";
            QString path = QFileDialog::getSaveFileName(
                this, "录制文件", defaultName, "sin 录制文件 (*.sin)");
            if (path.isEmpty()) {
                m_recordTab->setRecording(false);
                return;
            }
            if (!m_recorder->start(path)) {
                QMessageBox::warning(this, "录制", "无法创建文件: " + path);
                m_recordTab->setRecording(false);
                m_bottomPanel->addProblem(1, "Recorder", "无法创建录制文件: " + path);
                return;
            }
        } else {
            m_recorder->stop();
        }
    });

    // 侧边栏面板
    connect(m_sideBar->dbcPanel(), &DbcPanel::signalDoubleClicked,
            this, &MainWindow::onSignalDoubleClicked);
    connect(m_sideBar->dbcPanel(), &DbcPanel::dbcFileClicked,
            this, &MainWindow::onDbcFileClicked);
    connect(m_sideBar->tracePanel(), &TracePanel::openTraceRequested,
            this, &MainWindow::onOpenTraceTab);
    connect(m_sideBar->playbackPanel(), &PlaybackPanel::openPlaybackRequested,
            this, &MainWindow::onOpenPlaybackTab);
    connect(m_sideBar->recordPanel(), &RecordPanel::openRecordRequested,
            this, &MainWindow::onOpenRecordTab);
    connect(m_sideBar->graphicConfigPanel(), &GraphicConfigPanel::newGraphicRequested,
            this, &MainWindow::onNewGraphicRequested);
    connect(m_sideBar->settingsPanel(), &SettingsPanel::settingsRequested,
            this, &MainWindow::onSettingsRequested);
    connect(m_sideBar->devicePanel(), &DevicePanel::deviceConnectRequested,
            this, [this](const QString &, int) {
        m_connLabel->setText("🔗 已连接");
    });
    connect(m_sideBar->devicePanel(), &DevicePanel::deviceDisconnectRequested,
            this, [this]() {
        m_connLabel->setText("🔗 未连接");
    });

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
    m_traceTab->setDbcManager(m_dbcManager);

    // BottomPanel 命令
    connect(m_bottomPanel, &BottomPanel::commandEntered,
            this, &MainWindow::onCommandEntered);

    // DBC 加载通知
    connect(m_dbcManager, &DbcManager::dbcLoaded, this, [this](const QString &name) {
        m_bottomPanel->appendOutput("DBC 已加载: " + name);
    });

    m_bottomPanel->appendOutput("sin 启动完成");
    updateActions();
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
    m_openAction->setToolTip("打开录制文件 (.sin) 或 DBC 文件");
    fileMenu->addAction(m_openAction);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    auto *openProj = new QAction("打开工程...", this);
    openProj->setShortcut(QKeySequence("Ctrl+Shift+O"));
    fileMenu->addAction(openProj);

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
    toggleBottom->setChecked(true);
    viewMenu->addAction(toggleBottom);
    connect(toggleBottom, &QAction::triggered, this, &MainWindow::toggleBottomDock);

    auto *toggleRight = new QAction("右侧栏", this);
    toggleRight->setCheckable(true);
    toggleRight->setChecked(true);
    viewMenu->addAction(toggleRight);
    connect(toggleRight, &QAction::triggered, this, &MainWindow::toggleRightDock);

    viewMenu->addSeparator();
    viewMenu->addAction("重置布局", this, &MainWindow::resetLayout);

    // ---- 工具 ----
    auto *toolsMenu = menuBar()->addMenu("工具(&T)");

    m_recordAction = new QAction("● 录制", this);
    m_recordAction->setCheckable(true);
    m_recordAction->setShortcut(QKeySequence("Ctrl+R"));
    toolsMenu->addAction(m_recordAction);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    toolsMenu->addSeparator();

    m_playAction = new QAction("▶ 播放", this);
    m_playAction->setShortcut(QKeySequence(Qt::Key_Space));
    toolsMenu->addAction(m_playAction);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_pauseAction = new QAction("⏸ 暂停", this);
    toolsMenu->addAction(m_pauseAction);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);

    m_stopAction = new QAction("⏹ 停止", this);
    toolsMenu->addAction(m_stopAction);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    toolsMenu->addSeparator();

    m_clearAction = new QAction("清空 Trace", this);
    toolsMenu->addAction(m_clearAction);
    connect(m_clearAction, &QAction::triggered, this, &MainWindow::onClear);

    m_autoScrollAction = new QAction("自动滚动", this);
    m_autoScrollAction->setCheckable(true);
    m_autoScrollAction->setChecked(true);
    toolsMenu->addAction(m_autoScrollAction);
    connect(m_autoScrollAction, &QAction::toggled, this, &MainWindow::onAutoScrollToggled);

    toolsMenu->addSeparator();

    m_simAction = new QAction("模拟器开关", this);
    m_simAction->setCheckable(true);
    toolsMenu->addAction(m_simAction);
    connect(m_simAction, &QAction::toggled, this, [this](bool on) {
        if (on) m_simulator->start();
        else    m_simulator->stop();
    });

    // ---- 帮助 ----
    auto *helpMenu = menuBar()->addMenu("帮助(&H)");

    helpMenu->addAction("关于 sin", this, &MainWindow::showAboutDialog);
    helpMenu->addSeparator();
    helpMenu->addAction("文档", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/JakeCai/sin/wiki"));
    });
    helpMenu->addAction("官方网站", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/JakeCai/sin"));
    });
    helpMenu->addAction("GitHub 仓库", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/JakeCai/sin"));
    });
    helpMenu->addSeparator();
    helpMenu->addAction("报告问题", this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/JakeCai/sin/issues"));
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
    container->setFixedHeight(28);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_minBtn = new QToolButton(container);
    m_minBtn->setObjectName("WinMinBtn");
    m_minBtn->setText("\u2500");
    m_minBtn->setFixedSize(46, 28);
    m_minBtn->setAutoRaise(true);
    m_minBtn->setToolTip("最小化");

    m_maxBtn = new QToolButton(container);
    m_maxBtn->setObjectName("WinMaxBtn");
    m_maxBtn->setText("\u25a1");
    m_maxBtn->setFixedSize(46, 28);
    m_maxBtn->setAutoRaise(true);
    m_maxBtn->setToolTip("最大化");

    m_closeBtn = new QToolButton(container);
    m_closeBtn->setObjectName("WinCloseBtn");
    m_closeBtn->setText("\u2715");
    m_closeBtn->setFixedSize(46, 28);
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
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // ---- 中央: 可拆分编辑器区域 ----
    m_editorArea = new SplitEditorArea(this);

    // Trace 标签页
    m_traceTab = new TraceTab(this);
    m_traceTab->setProxyModel(m_proxyModel);
    m_editorArea->addTab(m_traceTab, "📋 Trace");

    // Graphic 标签页
    m_graphicView = new GraphicView(this);
    m_editorArea->addTab(m_graphicView, "📈 Graphic1");

    // 回放控制标签页
    m_playbackTab = new PlaybackTab(this);
    m_editorArea->addTab(m_playbackTab, "▶ 回放控制");

    // 录制控制标签页
    m_recordTab = new RecordTab(this);
    m_editorArea->addTab(m_recordTab, "● 录制控制");

    setCentralWidget(m_editorArea);

    // ---- 右侧 Dock ----
    m_rightPanel = new RightPanel(this);
    m_rightDock = new QDockWidget("右侧栏", this);
    m_rightDock->setObjectName("RightDock");
    m_rightDock->setWidget(m_rightPanel);
    m_rightDock->setFeatures(QDockWidget::DockWidgetMovable |
                             QDockWidget::DockWidgetClosable |
                             QDockWidget::DockWidgetFloatable);
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    // ---- 底部 Dock ----
    m_bottomPanel = new BottomPanel(this);
    m_bottomDock = new QDockWidget("输出", this);
    m_bottomDock->setObjectName("BottomDock");
    m_bottomDock->setWidget(m_bottomPanel);
    m_bottomDock->setFeatures(QDockWidget::DockWidgetMovable |
                              QDockWidget::DockWidgetClosable |
                              QDockWidget::DockWidgetFloatable);
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);

    resizeDocks({m_leftDock}, {300}, Qt::Horizontal);
    resizeDocks({m_rightDock}, {260}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {180}, Qt::Vertical);
}

// ============================================================
//  状态栏
// ============================================================

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel("就绪", this);
    m_connLabel = new QLabel("🔗 未连接", this);
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
        m_sideBar->setVisible(true);
        m_sideBarVisible = true;
    }

    // 联动主标签页
    auto *tabs = m_editorArea->activeTabWidget();
    if (!tabs) return;

    if (activity == ActivityBar::Trace) {
        onOpenTraceTab();
    } else if (activity == ActivityBar::Graphic) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i).contains("Graphic")) {
                tabs->setCurrentIndex(i);
                break;
            }
        }
    } else if (activity == ActivityBar::Playback) {
        onOpenPlaybackTab();
    } else if (activity == ActivityBar::Record) {
        onOpenRecordTab();
    }
}

void MainWindow::onActivityToggled(int)
{
    m_sideBarVisible = !m_sideBarVisible;
    m_sideBar->setVisible(m_sideBarVisible);
    // ActivityBar 始终可见，不隐藏
}

// ============================================================
//  录制 / 回放
// ============================================================

void MainWindow::onRecord()
{
    if (m_recording) {
        m_recorder->stop();
    } else {
        QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".sin";
        QString path = QFileDialog::getSaveFileName(
            this, "录制文件", defaultName, "sin 录制文件 (*.sin)");
        if (path.isEmpty()) {
            m_recordAction->setChecked(false);
            return;
        }
        if (!m_recorder->start(path)) {
            QMessageBox::warning(this, "录制", "无法创建文件: " + path);
            m_recordAction->setChecked(false);
            m_bottomPanel->addProblem(1, "Recorder", "无法创建录制文件: " + path);
            return;
        }
    }
}

void MainWindow::onPlay()
{
    if (!m_player->isLoaded()) {
        onOpenFile();
        if (!m_player->isLoaded()) return;
    }
    m_player->play();
}

void MainWindow::onPause() { m_player->pause(); }
void MainWindow::onStop()  { m_player->stop(); }

void MainWindow::onClear()
{
    m_traceModel->clear();
    m_graphicView->clearData();
    m_traceTab->frameInfo()->clear();
    m_traceTab->signalDecode()->clear();
    updateStatistics();
}

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "打开文件", {},
        "sin 录制文件 (*.sin);;DBC 文件 (*.dbc);;所有文件 (*.*)");
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
        m_traceModel->clear();
        m_graphicView->clearData();
        m_bottomPanel->appendOutput(QString("已加载: %1 (%2 帧, %3s)")
            .arg(fi.fileName()).arg(m_player->totalFrames())
            .arg(m_player->totalTime(), 0, 'f', 2));
        m_playbackTab->setFileInfo(fi.fileName(), m_player->totalFrames(), m_player->totalTime());
    }
    updateActions();
}

void MainWindow::onAutoScrollToggled(bool on)
{
    m_autoScroll = on;
    m_traceTab->traceView()->setAutoScrollEnabled(on);
}

// ============================================================
//  数据流
// ============================================================

void MainWindow::onFrameReceived(const CanFrame &frame)
{
    m_traceModel->appendFrame(frame);
    m_graphicView->onFrame(frame);
    if (m_recording)
        m_recorder->recordFrame(frame);
    if (m_autoScroll)
        m_traceTab->traceView()->scrollToBottom();
    m_frameCountLabel->setText(QString::number(m_traceModel->frameCount()) + " 帧");
    m_rowCountLabel->setText(QString::number(m_traceModel->frameCount()) + "行");
}

void MainWindow::onFramePlayed(const CanFrame &frame)
{
    onFrameReceived(frame);
}

// ============================================================
//  过滤
// ============================================================

void MainWindow::onFilterApplied(const QString &filter)
{
    if (!m_proxyModel->setFilterExpression(filter))
        m_bottomPanel->addProblem(0, "Filter", "语法错误: " + filter);
    else
        m_bottomPanel->appendOutput("过滤已应用: " + filter);
    int total = m_traceModel->frameCount();
    int filtered = m_proxyModel->rowCount();
    m_filterLabel->setText(QString("过滤%1/%2").arg(filtered).arg(total));
}

void MainWindow::onFilterCleared()
{
    m_proxyModel->clearFilter();
    int total = m_traceModel->frameCount();
    m_filterLabel->setText(QString("过滤%1/%2").arg(total).arg(total));
}

// ============================================================
//  Trace 选择
// ============================================================

void MainWindow::onTraceSelectionChanged()
{
    const CanFrame *frame = m_traceTab->traceView()->selectedFrame();
    if (!frame) {
        m_selectedLabel->setText("选中0行");
        return;
    }
    m_selectedLabel->setText("选中1行");
}

void MainWindow::onFrameDoubleClicked(const CanFrame &frame)
{
    QString filter = QString("id == %1").arg(CanUtils::formatId(frame.id, frame.extended));
    auto *edit = m_traceTab->filterBar()->findChild<QLineEdit *>();
    if (edit) edit->setText(filter);
    m_proxyModel->setFilterExpression(filter);

    GraphicView::Signal sig;
    sig.name = QString("ID_%1[0]").arg(frame.id, 0, 16).toUpper();
    sig.canId = frame.id & 0x1FFFFFFF;
    sig.extended = frame.extended;
    sig.byteOffset = 0;
    sig.bitLength = 8;
    m_graphicView->addSignal(sig);

    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->widget(i) == m_graphicView) {
                tabs->setCurrentIndex(i);
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
    bool hasFile = m_player->isLoaded();
    m_playbackTab->setPlayerLoaded(hasFile, playing);
    m_statusLabel->setText(playing ? "回放中..." : "已暂停");
}

void MainWindow::onPlayerFinished()
{
    m_statusLabel->setText("回放完成");
    updateActions();
    m_playbackTab->setPlayerLoaded(m_player->isLoaded(), false);
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
    gsig.byteOffset = sig->startBit / 8;
    gsig.bitLength = qMax(8, sig->bitLength / 8 * 8);
    gsig.bigEndian = !sig->littleEndian;
    m_graphicView->addSignal(gsig);

    m_bottomPanel->appendOutput(QString("已添加信号: %1 (ID=0x%2)")
        .arg(signalName).arg(canId, 0, 16).toUpper());
}

void MainWindow::onDbcFileClicked(const QString &fileName)
{
    // 打开 DBC 详情标签页
    auto *dbcTab = new DbcDetailTab(fileName, m_dbcManager, this);
    connect(dbcTab, &DbcDetailTab::signalDoubleClicked,
            this, &MainWindow::onSignalDoubleClicked);
    openTab(dbcTab, "📄 DBC: " + fileName);
}

// ============================================================
//  侧边栏入口
// ============================================================

void MainWindow::openTab(QWidget *widget, const QString &label)
{
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        // 检查是否已存在同名标签
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == label) {
                tabs->setCurrentIndex(i);
                return;
            }
        }
        tabs->addTab(widget, label);
        tabs->setCurrentIndex(tabs->count() - 1);
    }
    m_tabLabel->setText(label);
}

void MainWindow::onOpenTraceTab()
{
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i).contains("Trace")) {
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(tabs->tabText(i));
                return;
            }
        }
    }
}

void MainWindow::onOpenPlaybackTab()
{
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i).contains("回放")) {
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(tabs->tabText(i));
                return;
            }
        }
    }
}

void MainWindow::onOpenRecordTab()
{
    auto *tabs = m_editorArea->activeTabWidget();
    if (tabs) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i).contains("录制")) {
                tabs->setCurrentIndex(i);
                m_tabLabel->setText(tabs->tabText(i));
                return;
            }
        }
    }
}

void MainWindow::onNewGraphicRequested()
{
    static int graphicCount = 2;
    auto *gv = new GraphicView(this);
    openTab(gv, QString("📈 Graphic%1").arg(graphicCount++));
}

void MainWindow::onSettingsRequested(const QString &section)
{
    QMessageBox::information(this, "设置", "设置: " + section + "\n(待实现)");
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
    // 通过模拟器连接
    if (!m_simulator->isRunning()) {
        m_simulator->start();
        m_connLabel->setText("🔗 已连接");
        m_bottomPanel->appendOutput("设备已连接 (模拟器)");
    }
}

void MainWindow::onQuickDisconnect()
{
    m_simulator->stop();
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
        onFilterApplied(expr);
        out->appendTerminal("过滤: " + expr);
    } else if (cmd == "stats") {
        updateStatistics();
        out->appendTerminal(QString("总帧数: %1").arg(m_traceModel->frameCount()));
    } else if (cmd.startsWith("load ")) {
        QString path = cmd.mid(5).trimmed();
        QFileInfo fi(path);
        if (fi.suffix().toLower() == "dbc") {
            m_dbcManager->loadDbc(path);
        } else if (fi.suffix().toLower() == "sin") {
            if (m_player->load(path)) {
                m_traceModel->clear();
                out->appendTerminal("已加载录制: " + fi.fileName());
                updateActions();
            }
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
    int total = m_traceModel->frameCount();
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
    m_playbackTab->setPlayerLoaded(hasFile, playing);
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
    resizeDocks({m_leftDock}, {300}, Qt::Horizontal);
    resizeDocks({m_rightDock}, {260}, Qt::Horizontal);
    resizeDocks({m_bottomDock}, {180}, Qt::Vertical);
}

// ============================================================
//  帮助菜单对话框
// ============================================================

void MainWindow::showAboutDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("关于 sin");
    dlg.setFixedWidth(380);
    auto *layout = new QVBoxLayout(&dlg);

    auto *title = new QLabel("<b style='font-size:24px;color:#4a90d9'>sin</b>", &dlg);
    auto *desc = new QLabel("CAN/CAN FD 报文分析工具", &dlg);
    auto *ver = new QLabel("版本: 1.0.0", &dlg);
    auto *author = new QLabel("作者: 蔡可杰 (Jake.cai)", &dlg);
    auto *github = new QLabel("GitHub: <a href='https://github.com/JakeCai/sin'>https://github.com/JakeCai/sin</a>", &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *email = new QLabel("邮箱: 929168503@qq.com", &dlg);
    auto *wechat = new QLabel("微信: 13368295840", &dlg);
    auto *biz = new QLabel("商业合作: 929168503@qq.com / 微信 13368295840", &dlg);
    biz->setStyleSheet("font-size: 11px; color: gray;");
    auto *copyright = new QLabel("基于 Qt6 构建 © 2026", &dlg);
    copyright->setStyleSheet("font-size: 11px; color: gray;");

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
    browser->setStyleSheet("font-family: Consolas, monospace; font-size: 12px;");
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
        "如有更新，请前往 GitHub Releases 页面下载最新版本。");
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
    auto *github = new QLabel("GitHub: <a href='https://github.com/JakeCai/sin'>https://github.com/JakeCai/sin</a>", &dlg);
    github->setTextInteractionFlags(Qt::TextBrowserInteraction);
    github->setOpenExternalLinks(true);
    auto *note = new QLabel("本项目基于 MIT License 开源，商业使用请联系作者获取授权。", &dlg);
    note->setStyleSheet("font-size: 11px; color: gray;");
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
    m_player->stop();
    event->accept();
}
