#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QJsonValue>
#include <QVariant>
#include "core/canframe.h"

class CanTraceModel;
class TraceTab;
class TraceView;
class GraphicView;
class FilterBar;
class FrameInfoWidget;
class SignalDecodeWidget;
class SplitEditorArea;
// class DbcDetailTab 随 DBC 页迁入 openbus_dbc.dll（拆分方案 B3）
class Recorder;
class Player;
class CanSimulator;
class CanDeviceManager;
class DbcManager;
struct ShellContext;
class ActivityBar;
class SideBar;
class BottomPanel;
class RightPanel;
// MeasurementSetupView / DeviceConnectionTab 随 Flow 页迁入 openbus_flow.dll（拆分方案 B4）
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
    void changeEvent(QEvent *event) override;
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
    // onTriggerRecording 随 setupRecordTab 迁入 transceive 模块（拆分方案 B2）

    // 工程
    void onOpenProject();
    void onSaveProject();
    void onProjectSwitched(int index);
    void onProjectCreated(const QString &name);
    void onFilePreviewRequested(const QString &filePath);
    void captureProjectState();
    void applyProjectState();

    // 测量启停编排（离线加载 + Trace/Graphic 门控；flow 经 invoke("measurementToggled")
    // 直调——slot 声明保证元对象可达（L2 UI 驱动测试经 invokeMethod 驱动，DEF-09）
    void onMeasurementToggled(bool running);

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
    void refreshWindowButtonIcons();   // 窗口按钮 SVG 图标（主题色 + 最大化/还原切换）
    void updateActions();
    void updateStatistics();
    void setupTraceTab(TraceTab *tab);
    // setupSendTab/setupPlaybackTab/setupOfflineAnalysisTab/setupRecordTab
    // 已迁入 TransceiveModule（拆分方案 B2：模块自己连接自己的信号槽）
    // setupDeviceTab/setupMeasurementTab 已迁入 FlowModule（拆分方案 B4）
    void processCommand(const QString &cmd);
    void openTab(QWidget *widget, const QString &label);
    void refreshPanelLists();
    void setupMarketTab();  // 创建/重建插件市场页（经 ModuleRegistry "market" 模块，方案 §13 / 拆分方案 B0）
    void marketInvoke(const QString &action, const QVariant &arg = {});  // 市场模块动作转发（invoke 字符串约定见 imodule.h）
    void linkGraphicCursor(GraphicView *gv);  // 新建 GraphicView 时与已有视图建立游标联动

    // ---- transceive 模块（拆分方案 B2）----
    ShellContext makeShellContext();           // 构造含数据层服务指针与壳回调的上下文
    void transceiveInvoke(const QString &action, const QVariant &arg = {});  // 收发模块动作转发
    QVariant transceiveQuery(const QString &what, const QVariant &arg = {}); // 收发模块查询转发

    // ---- flow 模块（拆分方案 B4）----
    void flowInvoke(const QString &action, const QVariant &arg = {});   // flow 模块动作转发
    QVariant flowQuery(const QString &what, const QVariant &arg = {});  // flow 模块查询转发
    void onModuleToggled(const QString &blockId, const QString &name, bool enabled);
    void onModuleOpened(const QString &moduleId, const QString &instanceId);
    void onModuleInstanceClosed(const QString &moduleId, const QString &instanceId);
    void openDevicePage();                         // 查找/新建设备连接页（无参变体，Real 块入口）
    void unloadDbcFile(const QString &fileName);   // DBC 卸载：关关联标签页 + unloadDbc

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
    // m_sendTab/m_playbackTab/m_offlineTab/m_recordTab 随收发四页迁入
    // openbus_transceive.dll（拆分方案 B2；壳经 createPage/invoke/query 操控）
    // m_deviceTab/m_setupView 随 Flow 页迁入 openbus_flow.dll（拆分方案 B4；页面单实例缓存在模块内）
    QWidget *m_marketWidget = nullptr;   // 统一插件市场（经 ModuleRegistry "market" 模块创建，方案 §13 / 拆分方案 B0）

    // ---- 核心引擎 ----
    Recorder *m_recorder = nullptr;
    Player *m_player = nullptr;
    // m_triggerRecorder 随录制页迁入 transceive 模块（B2）
    CanSimulator *m_simulator = nullptr;
    CanDeviceManager *m_deviceManager = nullptr;  ///< 硬件设备管理器（ZLG/PEAK/...）

    // 周期发送定时器（m_periodicSenders）随发送页迁入 transceive 模块（B2）

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

    // ---- 窗口控制按钮 ----
    QToolButton *m_minBtn = nullptr;
    QToolButton *m_maxBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QPoint m_dragPosition;
};

#endif // MAINWINDOW_H
