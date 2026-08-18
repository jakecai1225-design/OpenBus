#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QJsonValue>
#include "core/canframe.h"

class CanTraceModel;
class TraceTab;
class TraceView;
class GraphicView;
class FilterBar;
class FrameInfoWidget;
class SignalDecodeWidget;
class SplitEditorArea;
class SignalSendTab;
class PlaybackTab;
class OfflineAnalysisTab;
class RecordTab;
class DbcDetailTab;
class Recorder;
class Player;
class CanSimulator;
class CanDeviceManager;
class TriggerRecorder;
class DbcManager;
class ActivityBar;
class SideBar;
class BottomPanel;
class RightPanel;
class MeasurementSetupView;
class DeviceConnectionTab;
class MarketTab;
class PluginDetailPage;
class BusStatistics;
class FilterPresetManager;
class BookmarkManager;
class DataWindow;
class IOGraphView;
class PluginManager;
struct DbcFile;
class QAction;
class QSlider;
class QComboBox;
class QTabWidget;
class QDockWidget;
class QToolButton;
class QTimer;

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
    void onImportLog();
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
    void onSignalAddToTrace(quint32 canId, const QString &signalName);

    // 侧边栏入口
    void onOpenTraceTab();
    void onTracePageSelected(int row);
    void onGraphicPageSelected(int row);
    void onOpenSendTab();
    void onOpenPlaybackTab();
    void onOpenOfflineAnalysisTab();
    void onOpenRecordTab();
    void onNewGraphicRequested();
    void onOpenMeasurementSetup();
    void onOpenDeviceTab(int deviceKind, int devIndex, const QString &deviceName, int deviceType);
    void onOpenMarketTab();   // 插件市场标签页（＋新增设备跳转入口，方案 §13.6）
    void onSettingsRequested(const QString &section);

    // P0/P1 新增
    void onOpenDataWindow();
    void onOpenIOGraph();
    void onOpenColorRuleEditor();
    void onBookmarkJumped(int frameIndex);
    void onTriggerRecording(const QString &dir, const QString &prefix,
                              const QString &format, bool splitBySize, int sizeMb,
                              bool splitByTime, int timeSec, bool ringMode,
                              int maxFiles, const QString &triggerExpr,
                              double preTriggerSec, double postTriggerSec,
                              bool repeatTrigger);

    // 工程
    void onOpenProject();
    void onSaveProject();
    void onProjectSwitched(int index);
    void onProjectCreated(const QString &name);
    void onFilePreviewRequested(const QString &filePath);
    void captureProjectState();
    void applyProjectState();

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

    // 插件
    void onPluginOutput(const QString &text);
    void onPluginCommandRegistered(const QString &id, const QString &title);
    void onPluginSendFrame(const CanFrame &frame);
    void onPluginRequestSelectedFrames(const QJsonValue &requestId);

private:
    void createMenuBar();
    void createLayout();
    void createStatusBar();
    void createWindowButtons();
    void updateActions();
    void updateStatistics();
    void setupTraceTab(TraceTab *tab);
    void setupSendTab(SignalSendTab *tab);
    void setupPlaybackTab(PlaybackTab *tab);
    void setupOfflineAnalysisTab(OfflineAnalysisTab *tab);
    void setupRecordTab(RecordTab *tab);
    void setupDeviceTab(DeviceConnectionTab *tab);
    void processCommand(const QString &cmd);
    void openTab(QWidget *widget, const QString &label);
    void refreshPanelLists();
    void refreshPluginList();
    void setupMarketTab();  // 创建/重建 MarketTab（统一插件市场）并连接信号
    void openPluginDetail(const QString &name);  // 打开插件详情页（多插件共用一个标签页）
    void linkGraphicCursor(GraphicView *gv);  // 新建 GraphicView 时与已有视图建立游标联动

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

    // ---- P0/P1 核心服务 ----
    BusStatistics *m_busStats = nullptr;
    FilterPresetManager *m_filterPresets = nullptr;
    BookmarkManager *m_bookmarkMgr = nullptr;
    DataWindow *m_dataWindow = nullptr;
    IOGraphView *m_ioGraph = nullptr;

    // ---- 插件系统 ----
    PluginManager *m_pluginManager = nullptr;

    // ---- UI (Main tabs) ----
    TraceTab *m_traceTab = nullptr;
    GraphicView *m_graphicView = nullptr;
    SignalSendTab *m_sendTab = nullptr;
    PlaybackTab *m_playbackTab = nullptr;
    OfflineAnalysisTab *m_offlineTab = nullptr;
    RecordTab *m_recordTab = nullptr;
    DeviceConnectionTab *m_deviceTab = nullptr;
    MarketTab *m_marketTab = nullptr;   // 统一插件市场（驱动 + 插件，方案 §13）
    PluginDetailPage *m_pluginDetailPage = nullptr;

    // ---- 核心引擎 ----
    Recorder *m_recorder = nullptr;
    Player *m_player = nullptr;
    TriggerRecorder *m_triggerRecorder = nullptr;
    CanSimulator *m_simulator = nullptr;
    CanDeviceManager *m_deviceManager = nullptr;  ///< 硬件设备管理器（ZLG/PEAK/...）

    // ---- 周期发送 ----
    QHash<int, QTimer *> m_periodicSenders;  ///< 行号 → 周期发送定时器

    // ---- 菜单 Action ----
    QAction *m_recordAction = nullptr;
    QAction *m_playAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_clearAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_importAction = nullptr;
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
    bool m_measurementRunning = false;  ///< 全局测量运行状态（由 flow 标签页控制）
    bool m_sideBarVisible = true;
    int m_savedDockWidth = 300;
    int m_traceCount = 0;
    int m_graphicCount = 0;
    int m_receivedFrameCount = 0;  ///< 测量期间累计接收的帧数（状态栏显示）

    // ---- 实例跟踪（flow 页面模块实例）----
    QMap<QString, QWidget*> m_traceInstances;    // "trace1" → TraceTab*
    QMap<QString, QWidget*> m_graphicInstances;  // "graphic1" → GraphicView*
    MeasurementSetupView *m_setupView = nullptr;

    // ---- 窗口控制按钮 ----
    QToolButton *m_minBtn = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QPoint m_dragPosition;
};

#endif // MAINWINDOW_H
