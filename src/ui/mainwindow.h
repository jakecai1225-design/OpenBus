#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include "core/canframe.h"

class CanTraceModel;
class CanFilterProxyModel;
class TraceView;
class GraphicView;
class FilterBar;
class Recorder;
class Player;
class CanSimulator;
class QAction;
class QSlider;
class QComboBox;

/**
 * @brief CANoe 风格主窗口
 *
 * 三段式布局：
 *   1. 工具栏（录制 / 回放 / 清空 / 模拟器 / 自动滚动）
 *   2. 过滤栏（Wireshark 风格显示过滤）
 *   3. 可拖拽分割区域（Trace 报文表 + Graphic 信号图）
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // 工具栏
    void onRecord();
    void onPlay();
    void onPause();
    void onStop();
    void onClear();
    void onOpenFile();
    void onSimulatorToggled(bool on);
    void onAutoScrollToggled(bool on);

    // 数据流
    void onFrameReceived(const CanFrame &frame);
    void onFramePlayed(const CanFrame &frame);

    // 过滤
    void onFilterApplied(const QString &filter);
    void onFilterCleared();

    // 回放进度
    void onPlayerProgress(int cur, int total, double curTime, double totalTime);
    void onPlayerStateChanged(bool playing);
    void onPlayerFinished();
    void onSpeedChanged(int index);
    void onSeekChanged(int value);

    // Trace 双击 → 添加到 Graphic
    void onFrameDoubleClicked(const CanFrame &frame);

private:
    void createToolBar();
    void createCentralWidget();
    void createStatusBar();
    void updateActions();
    void updateStatus();

    // ---- 数据 ----
    CanTraceModel *m_traceModel = nullptr;
    CanFilterProxyModel *m_proxyModel = nullptr;

    // ---- UI ----
    TraceView *m_traceView = nullptr;
    GraphicView *m_graphicView = nullptr;
    FilterBar *m_filterBar = nullptr;
    QSlider *m_seekSlider = nullptr;
    QComboBox *m_speedCombo = nullptr;

    // 工具栏 Actions
    QAction *m_recordAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_clearAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_autoScrollAction = nullptr;
    QAction *m_simulatorAction = nullptr;

    // ---- 核心引擎 ----
    Recorder *m_recorder = nullptr;
    Player *m_player = nullptr;
    CanSimulator *m_simulator = nullptr;

    // ---- 状态 ----
    QLabel *m_statusLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_timeLabel = nullptr;
    bool m_autoScroll = true;
    bool m_recording = false;
};

#endif // MAINWINDOW_H
