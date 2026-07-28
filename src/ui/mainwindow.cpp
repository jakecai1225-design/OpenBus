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
#include <QCheckBox>
#include <QLineEdit>
#include <QInputDialog>
#include <QToolButton>
#include <QMouseEvent>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("sin - CAN/CAN FD 报文分析工具");
    resize(1400, 900);

    // 无边框窗口 — 保留原生调整大小和 Aero Snap
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

    // 菜单栏安装事件过滤器 — 用于窗口拖拽
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
        m_sideBar->devicePanel()->setRecording(true);
        updateActions();
    });
    connect(m_recorder, &Recorder::recordingStopped, this, [this](const QString &path, int count) {
        m_recording = false;
        m_bottomPanel->appendOutput(QString("录制结束: %1 (%2 帧)").arg(path).arg(count));
        m_sideBar->devicePanel()->setRecording(false);
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

    // 回放控制 (来自 TraceConfigPanel)
    auto *tcp = m_sideBar->traceConfigPanel();
    connect(tcp, &TraceConfigPanel::playRequested, this, &MainWindow::onPlay);
    connect(tcp, &TraceConfigPanel::pauseRequested, this, &MainWindow::onPause);
    connect(tcp, &TraceConfigPanel::stopRequested, this, &MainWindow::onStop);
    connect(tcp, &TraceConfigPanel::speedChanged, this, &MainWindow::onSpeedChanged);
    connect(tcp, &TraceConfigPanel::seekChanged, this, &MainWindow::onSeekChanged);

    // 录制控制 (来自 DevicePanel)
    auto *dp = m_sideBar->devicePanel();
    connect(dp, &DevicePanel::recordToggled, this, [this](bool on) {
        if (on) {
            QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".sin";
            QString path = QFileDialog::getSaveFileName(
                this, "录制文件", defaultName, "sin 录制文件 (*.sin)");
            if (path.isEmpty()) {
                m_sideBar->devicePanel()->setRecording(false);
                return;
            }
            if (!m_recorder->start(path)) {
                QMessageBox::warning(this, "录制", "无法创建文件: " + path);
                m_sideBar->devicePanel()->setRecording(false);
                m_bottomPanel->addProblem(1, "Recorder", "无法创建录制文件: " + path);
                return;
            }
        } else {
            m_recorder->stop();
        }
    });
    connect(dp, &DevicePanel::clearRequested, this, &MainWindow::onClear);
    connect(dp, &DevicePanel::autoScrollToggled, this, &MainWindow::onAutoScrollToggled);

    // SideBar 面板
    connect(m_sideBar->dbcPanel(), &DbcPanel::signalDoubleClicked,
            this, &MainWindow::onSignalDoubleClicked);
    connect(m_sideBar->projectPanel(), &ProjectPanel::projectSwitched,
            this, &MainWindow::onProjectSwitched);
    connect(m_sideBar->traceConfigPanel(), &TraceConfigPanel::filterPresetApplied,
            this, &MainWindow::onFilterPresetApplied);
    connect(m_sideBar->traceConfigPanel(), &TraceConfigPanel::columnsChanged,
            this, &MainWindow::onColumnsChanged);

    // 设置面板关联
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
    m_openAction->setToolTip("打开录制文件 (.sin) 或 DBC 文件");
    fileMenu->addAction(m_openAction);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    fileMenu->addSeparator();
    fileMenu->addAction("退出(&Q)", this, &QApplication::quit);

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
    toolsMenu->addAction(m_recordAction);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    toolsMenu->addSeparator();

    m_playAction = new QAction("▶ 播放", this);
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
    helpMenu->addAction("关于 sin", this, [this]() {
        QMessageBox::about(this, "关于 sin",
            "<b>sin</b> - CAN/CAN FD 报文分析工具<br>"
            "版本 0.1.0<br><br>"
            "支持报文录制、回放、DBC 解析、Trace 追踪、Graphic 图形等功能。");
    });
}

// ============================================================
//  窗口控制按钮 (最小化 / 最大化 / 关闭)
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
    m_minBtn->setText("\u2500");  // ─
    m_minBtn->setFixedSize(46, 28);
    m_minBtn->setAutoRaise(true);
    m_minBtn->setToolTip("最小化");

    m_maxBtn = new QToolButton(container);
    m_maxBtn->setObjectName("WinMaxBtn");
    m_maxBtn->setText("\u25a1");  // □
    m_maxBtn->setFixedSize(46, 28);
    m_maxBtn->setAutoRaise(true);
    m_maxBtn->setToolTip("最大化");

    m_closeBtn = new QToolButton(container);
    m_closeBtn->setObjectName("WinCloseBtn");
    m_closeBtn->setText("\u2715");  // ✕
    m_closeBtn->setFixedSize(46, 28);
    m_closeBtn->setAutoRaise(true);
    m_closeBtn->setToolTip("关闭");

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    // 放到菜单栏右上角
    menuBar()->setCornerWidget(container, Qt::TopRightCorner);

    connect(m_minBtn, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_maxBtn, &QToolButton::clicked, this, [this]() {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
    });
    connect(m_closeBtn, &QToolButton::clicked, this, &QWidget::close);
}

// ============================================================
//  停靠面板布局
// ============================================================

void MainWindow::createLayout()
{
    // ---- 左侧 Dock: ActivityBar + SideBar ----
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
    m_leftDock->setTitleBarWidget(new QWidget()); // 隐藏标题栏
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // ---- 中央: 可拆分编辑器区域 ----
    m_editorArea = new SplitEditorArea(this);

    // Trace 标签页
    m_traceTab = new TraceTab(this);
    m_traceTab->setProxyModel(m_proxyModel);
    m_editorArea->addTab(m_traceTab, "📋 Trace");

    // Graphic 标签页
    m_graphicView = new GraphicView(this);
    m_editorArea->addTab(m_graphicView, "📈 Graphic");

    setCentralWidget(m_editorArea);

    // ---- 右侧 Dock ----
    m_rightPanel = new RightPanel(this);
    m_rightDock = new QDockWidget("属性", this);
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

    // 设置初始 Dock 尺寸
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
    m_frameCountLabel = new QLabel("0 帧", this);
    m_timeLabel = new QLabel("0.000s", this);

    statusBar()->addWidget(m_statusLabel, 1);
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
        m_leftDock->setVisible(true);
        m_sideBarVisible = true;
    }

    // 联动主标签页
    if (activity == ActivityBar::Trace) {
        auto *tabs = m_editorArea->activeTabWidget();
        if (tabs) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i).contains("Trace")) {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
    } else if (activity == ActivityBar::Graphic) {
        auto *tabs = m_editorArea->activeTabWidget();
        if (tabs) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i).contains("Graphic")) {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
    }
}

void MainWindow::onActivityToggled(int)
{
    m_sideBarVisible = !m_sideBarVisible;
    m_leftDock->setVisible(m_sideBarVisible);
}

// ============================================================
//  录制 / 回放槽函数
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
    m_rightPanel->clearAll();
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
}

void MainWindow::onFilterCleared()
{
    m_proxyModel->clearFilter();
}

void MainWindow::onFilterPresetApplied(const QString &filter)
{
    auto *edit = m_traceTab->filterBar()->findChild<QLineEdit *>();
    if (edit) edit->setText(filter);
    if (filter.isEmpty())
        onFilterCleared();
    else
        onFilterApplied(filter);
}

// ============================================================
//  Trace 选择 → 更新右侧属性
// ============================================================

void MainWindow::onTraceSelectionChanged()
{
    const CanFrame *frame = m_traceTab->traceView()->selectedFrame();
    if (!frame) return;

    // TraceTab 内部已更新帧结构和信号解析，这里只需更新右侧属性
    m_rightPanel->setFrameProperties(
        CanUtils::formatTime(frame->timestamp),
        QString::number(frame->channel),
        frame->direction == CanFrame::Rx ? "Rx" : "Tx",
        CanUtils::formatId(frame->id, frame->extended),
        CanUtils::formatDlc(frame->dlc, frame->fd),
        CanUtils::formatData(frame->data),
        CanUtils::formatFlags(*frame));
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
    m_sideBar->graphicConfigPanel()->setGraphicView(m_graphicView);

    // 跳转到 Graphic 标签页
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
    Q_UNUSED(cur); Q_UNUSED(total);
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
//  DBC 信号双击 → 添加到 Graphic
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

    m_sideBar->graphicConfigPanel()->setGraphicView(m_graphicView);
    m_bottomPanel->appendOutput(QString("已添加信号: %1 (ID=0x%2)")
        .arg(signalName).arg(canId, 0, 16).toUpper());
}

// ============================================================
//  工程切换
// ============================================================

void MainWindow::onProjectSwitched(int index)
{
    m_bottomPanel->appendOutput(QString("已切换到工程: %1")
        .arg(m_sideBar->projectPanel()->projects().at(index).name));
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
        onFilterPresetApplied(expr);
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
//  Trace 配置 — 列显示
// ============================================================

void MainWindow::onColumnsChanged()
{
    applyColumnVisibility();
}

void MainWindow::applyColumnVisibility()
{
    auto *panel = m_sideBar->traceConfigPanel();
    auto checkboxes = panel->findChildren<QCheckBox *>();
    for (auto *cb : checkboxes) {
        QString text = cb->text();
        int col = -1;
        if (text == "Time") col = CanTraceModel::ColTime;
        else if (text == "Channel") col = CanTraceModel::ColChannel;
        else if (text == "Direction") col = CanTraceModel::ColDirection;
        else if (text == "ID") col = CanTraceModel::ColId;
        else if (text == "DLC") col = CanTraceModel::ColDlc;
        else if (text == "Data") col = CanTraceModel::ColData;
        else if (text == "Flags") col = CanTraceModel::ColFlags;
        if (col >= 0)
            m_traceTab->traceView()->setColumnHidden(col, !cb->isChecked());
    }
}

// ============================================================
//  统计 & 状态
// ============================================================

void MainWindow::updateStatistics()
{
    int total = m_traceModel->frameCount();
    int rx = 0, tx = 0, fd = 0, ext = 0;
    for (const auto &f : m_traceModel->frames()) {
        if (f.direction == CanFrame::Rx) rx++; else tx++;
        if (f.fd) fd++;
        if (f.extended) ext++;
    }
    m_rightPanel->updateStatistics(total, rx, tx, fd, ext, 0.0);
    m_frameCountLabel->setText(QString::number(total) + " 帧");
}

void MainWindow::updateActions()
{
    bool hasFile = m_player->isLoaded();
    bool playing = m_player->isPlaying();
    m_playAction->setEnabled(hasFile && !playing);
    m_pauseAction->setEnabled(playing);
    m_stopAction->setEnabled(hasFile);
    m_recordAction->setChecked(m_recording);

    // 更新侧边栏回放按钮状态
    m_sideBar->traceConfigPanel()->setPlayerLoaded(hasFile, playing);
}

// ============================================================
//  视图菜单 — Dock 开关
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

void MainWindow::onTabContextMenu(int index, const QPoint &pos)
{
    Q_UNUSED(index);
    Q_UNUSED(pos);
    // SplitEditorArea 已内置右键菜单，此处留空
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
//  事件过滤器 — 菜单栏拖拽窗口 + 双击最大化
// ============================================================

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == menuBar()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                // 检查是否点在菜单项上 — 如果不在菜单项上才拖拽
                QAction *act = menuBar()->actionAt(me->pos());
                if (!act) {
                    // 调用系统级移动 — 保留 Aero Snap
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
//  Windows 原生事件 — 无边框窗口调整大小 + Aero Snap
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
