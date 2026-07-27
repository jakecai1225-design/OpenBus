#include "mainwindow.h"
#include "core/canframe.h"
#include "core/recorder.h"
#include "core/player.h"
#include "core/cansimulator.h"
#include "models/cantracemodel.h"
#include "models/canfilterproxymodel.h"
#include "ui/traceview.h"
#include "ui/graphicview.h"
#include "ui/filterbar.h"
#include "utils/canutils.h"

#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QStyle>
#include <QStatusBar>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("sin - CAN/CAN FD 报文分析工具");
    resize(1280, 800);

    // ---- 数据层 ----
    m_traceModel = new CanTraceModel(this);
    m_proxyModel = new CanFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_traceModel);

    // ---- 核心引擎 ----
    m_recorder  = new Recorder(this);
    m_player    = new Player(this);
    m_simulator = new CanSimulator(this);

    // ---- UI ----
    createToolBar();
    createCentralWidget();
    createStatusBar();

    // ---- 信号连接 ----

    // 模拟器 -> 帧接收
    connect(m_simulator, &CanSimulator::frameGenerated,
            this, &MainWindow::onFrameReceived);

    // 回放器 -> 帧播放
    connect(m_player, &Player::framePlayed,
            this, &MainWindow::onFramePlayed);
    connect(m_player, &Player::progressChanged,
            this, &MainWindow::onPlayerProgress);
    connect(m_player, &Player::stateChanged,
            this, &MainWindow::onPlayerStateChanged);
    connect(m_player, &Player::finished,
            this, &MainWindow::onPlayerFinished);

    // 录制器 -> 帧录制
    connect(m_recorder, &Recorder::recordingStarted, this, [this](const QString &) {
        m_recording = true;
        updateActions();
        updateStatus();
    });
    connect(m_recorder, &Recorder::recordingStopped, this, [this](const QString &, int) {
        m_recording = false;
        updateActions();
        updateStatus();
    });

    // 过滤栏
    connect(m_filterBar, &FilterBar::filterApplied,
            this, &MainWindow::onFilterApplied);
    connect(m_filterBar, &FilterBar::filterCleared,
            this, &MainWindow::onFilterCleared);

    // Trace 双击
    connect(m_traceView, &TraceView::frameDoubleClicked,
            this, &MainWindow::onFrameDoubleClicked);

    // 速度
    connect(m_speedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSpeedChanged);

    // Seek
    connect(m_seekSlider, &QSlider::sliderMoved,
            this, &MainWindow::onSeekChanged);

    updateActions();
    updateStatus();
}

MainWindow::~MainWindow() = default;

// ============================================================
//  工具栏
// ============================================================

void MainWindow::createToolBar()
{
    auto *tb = addToolBar("main");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));

    // 录制
    m_recordAction = new QAction("● 录制", this);
    m_recordAction->setToolTip("开始/停止录制报文到文件");
    m_recordAction->setCheckable(true);
    tb->addAction(m_recordAction);
    connect(m_recordAction, &QAction::triggered, this, &MainWindow::onRecord);

    tb->addSeparator();

    // 回放控制
    m_playAction = new QAction("▶ 播放", this);
    m_playAction->setToolTip("播放已加载的录制文件");
    tb->addAction(m_playAction);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::onPlay);

    m_pauseAction = new QAction("⏸ 暂停", this);
    m_pauseAction->setToolTip("暂停回放");
    tb->addAction(m_pauseAction);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::onPause);

    m_stopAction = new QAction("⏹ 停止", this);
    m_stopAction->setToolTip("停止回放");
    tb->addAction(m_stopAction);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::onStop);

    // 进度条
    tb->addSeparator();
    tb->addWidget(new QLabel("  位置: "));
    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setMinimum(0);
    m_seekSlider->setMaximum(1000);
    m_seekSlider->setFixedWidth(200);
    tb->addWidget(m_seekSlider);

    // 速度
    tb->addWidget(new QLabel("  速度: "));
    m_speedCombo = new QComboBox(this);
    m_speedCombo->addItem("0.25x", 0.25);
    m_speedCombo->addItem("0.5x", 0.5);
    m_speedCombo->addItem("1x", 1.0);
    m_speedCombo->addItem("2x", 2.0);
    m_speedCombo->addItem("4x", 4.0);
    m_speedCombo->addItem("8x", 8.0);
    m_speedCombo->setCurrentIndex(2);
    m_speedCombo->setFixedWidth(70);
    tb->addWidget(m_speedCombo);

    // 打开文件
    tb->addSeparator();
    m_openAction = new QAction("📂 打开", this);
    m_openAction->setToolTip("打开录制文件 (.sin)");
    tb->addAction(m_openAction);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::onOpenFile);

    tb->addSeparator();

    // 清空
    m_clearAction = new QAction("🗑 清空", this);
    m_clearAction->setToolTip("清空所有报文");
    tb->addAction(m_clearAction);
    connect(m_clearAction, &QAction::triggered, this, &MainWindow::onClear);

    // 自动滚动
    m_autoScrollAction = new QAction("自动滚动", this);
    m_autoScrollAction->setToolTip("新报文自动滚动到底部");
    m_autoScrollAction->setCheckable(true);
    m_autoScrollAction->setChecked(true);
    tb->addAction(m_autoScrollAction);
    connect(m_autoScrollAction, &QAction::toggled, this, &MainWindow::onAutoScrollToggled);

    // 模拟器
    m_simulatorAction = new QAction("模拟器", this);
    m_simulatorAction->setToolTip("启动/停止 CAN 报文模拟器");
    m_simulatorAction->setCheckable(true);
    tb->addAction(m_simulatorAction);
    connect(m_simulatorAction, &QAction::toggled, this, &MainWindow::onSimulatorToggled);
}

// ============================================================
//  中心区域 — 3 段式布局
// ============================================================

void MainWindow::createCentralWidget()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 第 1 段: 过滤栏
    m_filterBar = new FilterBar(this);
    layout->addWidget(m_filterBar);

    // 第 2+3 段: 可拖拽分割 — Trace 上 / Graphic 下
    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_traceView = new TraceView(this);
    m_traceView->setModel(m_proxyModel);

    m_graphicView = new GraphicView(this);

    splitter->addWidget(m_traceView);
    splitter->addWidget(m_graphicView);
    splitter->setStretchFactor(0, 3); // Trace 占 60%
    splitter->setStretchFactor(1, 2); // Graphic 占 40%
    splitter->setSizes({480, 320});

    layout->addWidget(splitter, 1);

    setCentralWidget(central);
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
//  工具栏槽函数
// ============================================================

void MainWindow::onRecord()
{
    if (m_recording) {
        m_recorder->stop();
    } else {
        QString defaultName = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".sin";
        QString path = QFileDialog::getSaveFileName(
            this, "选择录制文件", defaultName, "sin 录制文件 (*.sin)");
        if (path.isEmpty()) {
            m_recordAction->setChecked(false);
            return;
        }
        if (!m_recorder->start(path)) {
            QMessageBox::warning(this, "录制", "无法创建文件: " + path);
            m_recordAction->setChecked(false);
            return;
        }
    }
}

void MainWindow::onPlay()
{
    if (!m_player->isLoaded()) {
        onOpenFile();
        if (!m_player->isLoaded())
            return;
    }
    m_player->setSpeed(m_speedCombo->currentData().toDouble());
    m_player->play();
}

void MainWindow::onPause()
{
    m_player->pause();
}

void MainWindow::onStop()
{
    m_player->stop();
}

void MainWindow::onClear()
{
    m_traceModel->clear();
    m_graphicView->clearData();
    updateStatus();
}

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this, "打开录制文件", {}, "sin 录制文件 (*.sin)");
    if (path.isEmpty())
        return;

    if (!m_player->load(path)) {
        QMessageBox::warning(this, "打开文件", "无法加载文件: " + path);
        return;
    }

    // 加载到 Trace（清空后批量填充）
    m_traceModel->clear();
    m_graphicView->clearData();
    // 直接从 Player 获取帧
    // Player 没有公开 frames()，但我们可以通过 play 来逐帧回放
    // 或者先 clear trace，然后回放时自动填充
    updateActions();
    updateStatus();
    m_statusLabel->setText(QString("已加载: %1 (%2 帧, %3s)")
        .arg(path)
        .arg(m_player->totalFrames())
        .arg(m_player->totalTime(), 0, 'f', 2));
}

void MainWindow::onSimulatorToggled(bool on)
{
    if (on) {
        m_simulator->start();
        m_statusLabel->setText("模拟器运行中...");
    } else {
        m_simulator->stop();
        m_statusLabel->setText("模拟器已停止");
    }
    updateActions();
}

void MainWindow::onAutoScrollToggled(bool on)
{
    m_autoScroll = on;
    m_traceView->setAutoScrollEnabled(on);
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
        m_traceView->scrollToBottom();

    updateStatus();
}

void MainWindow::onFramePlayed(const CanFrame &frame)
{
    m_traceModel->appendFrame(frame);
    m_graphicView->onFrame(frame);

    if (m_autoScroll)
        m_traceView->scrollToBottom();

    updateStatus();
}

// ============================================================
//  过滤
// ============================================================

void MainWindow::onFilterApplied(const QString &filter)
{
    if (!m_proxyModel->setFilterExpression(filter)) {
        statusBar()->showMessage("过滤表达式语法错误: " + filter, 3000);
    }
}

void MainWindow::onFilterCleared()
{
    m_proxyModel->clearFilter();
}

// ============================================================
//  回放进度
// ============================================================

void MainWindow::onPlayerProgress(int cur, int total, double curTime, double totalTime)
{
    if (total > 0)
        m_seekSlider->setValue(static_cast<int>(curTime / totalTime * 1000));
    m_timeLabel->setText(QString::number(curTime, 'f', 3) + "s / " +
                          QString::number(totalTime, 'f', 3) + "s");
}

void MainWindow::onPlayerStateChanged(bool playing)
{
    updateActions();
    if (playing)
        m_statusLabel->setText("回放中...");
    else
        m_statusLabel->setText("已暂停");
}

void MainWindow::onPlayerFinished()
{
    m_statusLabel->setText("回放完成");
    m_seekSlider->setValue(0);
    updateActions();
}

void MainWindow::onSpeedChanged(int index)
{
    double speed = m_speedCombo->itemData(index).toDouble();
    m_player->setSpeed(speed);
}

void MainWindow::onSeekChanged(int value)
{
    if (!m_player->isLoaded())
        return;
    double ratio = value / 1000.0;
    m_player->seekTo(ratio * m_player->totalTime());
}

// ============================================================
//  Trace 双击 → 按 ID 过滤 + 添加到 Graphic
// ============================================================

void MainWindow::onFrameDoubleClicked(const CanFrame &frame)
{
    // 设置过滤栏为该 ID
    QString filter = QString("id == %1").arg(CanUtils::formatId(frame.id, frame.extended));
    m_filterBar->findChild<QLineEdit *>()->setText(filter);
    m_proxyModel->setFilterExpression(filter);

    // 同时在 GraphicView 中添加此信号的第一个字节监控
    GraphicView::Signal sig;
    sig.name = QString("ID_%1[0]").arg(frame.id, 0, 16).toUpper();
    sig.canId = frame.id & 0x1FFFFFFF;
    sig.extended = frame.extended;
    sig.byteOffset = 0;
    sig.bitLength = 8;
    m_graphicView->addSignal(sig);
}

// ============================================================
//  状态更新
// ============================================================

void MainWindow::updateActions()
{
    bool hasFile = m_player->isLoaded();
    bool playing = m_player->isPlaying();

    m_playAction->setEnabled(hasFile && !playing);
    m_pauseAction->setEnabled(playing);
    m_stopAction->setEnabled(hasFile);
    m_recordAction->setChecked(m_recording);
    m_simulatorAction->setEnabled(!playing); // 回放时不能同时模拟
}

void MainWindow::updateStatus()
{
    int count = m_traceModel->frameCount();
    m_frameCountLabel->setText(QString::number(count) + " 帧");

    if (m_recording)
        m_statusLabel->setText(QString("录制中... (%1 帧)").arg(m_recorder->frameCount()));
}

// ============================================================
//  关闭事件
// ============================================================

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_recording) {
        auto ret = QMessageBox::question(
            this, "退出", "正在录制中，确定退出吗？",
            QMessageBox::Yes | QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_recorder->stop();
    }
    m_simulator->stop();
    m_player->stop();
    event->accept();
}
