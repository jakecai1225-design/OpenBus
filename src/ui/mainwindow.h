#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include "core/canframe.h"

class CanTraceModel;
class CanFilterProxyModel;
class TraceTab;
class TraceView;
class GraphicView;
class FilterBar;
class FrameInfoWidget;
class SignalDecodeWidget;
class SplitEditorArea;
class SignalSendTab;
class PlaybackTab;
class RecordTab;
class DbcDetailTab;
class Recorder;
class Player;
class CanSimulator;
class DbcManager;
class ActivityBar;
class SideBar;
class BottomPanel;
class RightPanel;
class QAction;
class QSlider;
class QComboBox;
class QTabWidget;
class QDockWidget;
class QToolButton;

/**
 * @brief 主窗口 — 菜单栏 + QDockWidget 可停靠布局
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private slots:
    // ActivityBar
    void onActivityChanged(int activity);
    void onActivityToggled(int activity);

    // 菜单 / 录制 / 回放
    void onRecord();
    void onPlay();
    void onPause();
    void onStop();
    void onClear();
    void onOpenFile();
    void onAutoScrollToggled(bool on);

    // 数据流
    void onFrameReceived(const CanFrame &frame);
    void onFramePlayed(const CanFrame &frame);

    // Trace 选择
    void onTraceSelectionChanged();
    void onFrameDoubleClicked(const CanFrame &frame);

    // 回放
    void onPlayerProgress(int cur, int total, double curTime, double totalTime);
    void onPlayerStateChanged(bool playing);
    void onPlayerFinished();
    void onSpeedChanged(double speed);
    void onSeekChanged(double ratio);

    // DBC
    void onSignalDoubleClicked(quint32 canId, const QString &signalName);
    void onDbcFileClicked(const QString &fileName);

    // 侧边栏入口
    void onOpenTraceTab();
    void onTracePageSelected(int row);
    void onGraphicPageSelected(int row);
    void onOpenSendTab();
    void onOpenPlaybackTab();
    void onOpenRecordTab();
    void onNewGraphicRequested();
    void onSettingsRequested(const QString &section);

    // 右侧面板快捷按钮
    void onQuickRecord();
    void onQuickStopRecord();
    void onQuickConnect();
    void onQuickDisconnect();
    void onAiMessageSent(const QString &text);

    // 命令行
    void onCommandEntered(const QString &cmd);

    // 视图菜单
    void toggleLeftDock();
    void toggleRightDock();
    void toggleBottomDock();
    void resetLayout();

    // 帮助菜单对话框
    void showAboutDialog();
    void showLicenseDialog();
    void showReleaseNotes();
    void showShortcuts();
    void showCheckUpdate();
    void showBusinessCoop();

private:
    void createMenuBar();
    void createLayout();
    void createStatusBar();
    void createWindowButtons();
    void updateActions();
    void updateStatistics();
    void setupTraceTab(TraceTab *tab);
    void processCommand(const QString &cmd);
    void openTab(QWidget *widget, const QString &label);
    void refreshPanelLists();

    // ---- 布局 ----
    ActivityBar *m_activityBar = nullptr;
    SideBar *m_sideBar = nullptr;
    SplitEditorArea *m_editorArea = nullptr;
    BottomPanel *m_bottomPanel = nullptr;
    RightPanel *m_rightPanel = nullptr;
    QDockWidget *m_leftDock = nullptr;
    QDockWidget *m_rightDock = nullptr;
    QDockWidget *m_bottomDock = nullptr;

    // ---- 数据 ----
    DbcManager *m_dbcManager = nullptr;

    // ---- UI (Main tabs) ----
    TraceTab *m_traceTab = nullptr;
    GraphicView *m_graphicView = nullptr;
    SignalSendTab *m_sendTab = nullptr;
    PlaybackTab *m_playbackTab = nullptr;
    RecordTab *m_recordTab = nullptr;

    // ---- 核心引擎 ----
    Recorder *m_recorder = nullptr;
    Player *m_player = nullptr;
    CanSimulator *m_simulator = nullptr;

    // ---- 菜单 Action ----
    QAction *m_recordAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_clearAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_autoScrollAction = nullptr;
    QAction *m_simAction = nullptr;

    // ---- 状态栏 ----
    QLabel *m_statusLabel = nullptr;
    QLabel *m_connLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_tabLabel = nullptr;
    QLabel *m_rowCountLabel = nullptr;
    QLabel *m_selectedLabel = nullptr;
    QLabel *m_filterLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_timeLabel = nullptr;

    bool m_autoScroll = true;
    bool m_recording = false;
    bool m_sideBarVisible = true;
    int m_savedDockWidth = 300;
    int m_traceCount = 1;
    int m_graphicCount = 1;

    // ---- 窗口控制按钮 ----
    QToolButton *m_minBtn = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QPoint m_dragPosition;
};

#endif // MAINWINDOW_H
