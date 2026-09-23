#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QJsonValue>
#include <QVariant>
#include "core/canframe.h"

class CanTraceModel;
// TraceTab/TraceView/GraphicView/DataWindow 随 Trace/Graphic 页迁入
// openbus_trace.dll / openbus_graphic.dll（拆分方案 B5；壳经 ModuleRegistry 操控）
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
class BookmarkManager;
class IOGraphView;
class WatcherView;   // Watcher 观测页（壳侧单实例标签页，doc/Watcher方案.md 方案 A）
class PluginManager;
class CommandCenter;
class CommandPalette;
class SettingsPage;    // 设置页（标签页形态，原 SettingsDialog 弹窗改造）
class ShortcutsPage;   // 快捷键参考页（标签页形态）
class WelcomePage;     // VS Code-style welcome / start page
struct DbcFile;
struct DbcSignal;
class QAction;
class QSlider;
class QComboBox;
class QTabWidget;
class QDockWidget;
class QToolButton;
class QTimer;

/**
 * @brief 主窗口 — 菜单栏 + QDockWidget 可停靠布局
 *
 * B6 瘦身后按职责拆分为多个部分实现文件（同一类，见 doc/拆分应用实施方案.md §4.5）：
 *   mainwindow.cpp          壳核心：构造编排 + 模块编排（invoke/query/实例表）+ ShellContext
 *   mainwindow_setup.cpp    构造分阶段装配（服务/插件/数据管线/侧栏/工程接线）
 *   mainwindow_chrome.cpp   窗口骨架：菜单栏/窗口按钮/布局/状态栏/ActivityBar/dock 切换
 *   mainwindow_actions.cpp  用户动作：录制/回放/导入/快捷按钮/终端命令/统计
 *   mainwindow_frameflow.cpp 数据流：帧管线/回放进度/DBC 信号联动/测量门控编排
 *   mainwindow_pages.cpp    页面打开槽：侧边栏/市场/收发/设备/Flow/杂项页面
 *   mainwindow_project.cpp  工程与会话生命周期：打开/保存/切换/状态捕获/关闭
 *   mainwindow_dialogs.cpp  帮助对话框 + 插件集成槽
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

    // Data path
    void onFrameReceived(const CanFrame &frame);
    /// Bulk path (Phase A): one slot per drain / playback tick.
    void onFramesReceived(const QVector<CanFrame> &frames);
    void onFramePlayed(const CanFrame &frame);
    void onFramesPlayed(const QVector<CanFrame> &frames);

    // Trace/Graphic 联动（B5：模块经 shellInvoke 回调壳编排）
    void onFrameDoubleClicked(const CanFrame &frame);
    void onFrameAddToGraphic(const CanFrame &frame);

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
    void onOpenWelcomeTab();  // VS Code-style Welcome start page
    void onSettingsRequested(const QString &section);

    // P0/P1 新增
    void onOpenDataWindow();
    void onOpenWatcher();      // Watcher 观测页（doc/Watcher方案.md 方案 A）
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
    /// Clear Trace/Graphic and restart from the beginning (Flow Replay button).
    void onMeasurementReplay();
    /// Shared start path for first Play and Replay (replay=true clears then reloads).
    void startMeasurementSession(bool replay);

    // Flow 页面回调：Filter/DBC（双击块时由 Shell 触发）
    void onMeasurementViewFilterRequested();
    void onMeasurementViewDbcSelectRequested();

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
    void onPluginRequestRecentFrames(const QJsonValue &requestId, int count);
    void refreshWindowButtonIcons();   // 窗口按钮 SVG 图标（主题色 + 最大化/还原切换，DEF-08 字符串槽）

private:
    // ---- 构造分阶段装配（mainwindow_setup.cpp；原构造函数直排代码按阶段拆出）----
    void setupCoreServices();        // 数据层/P0P1 服务/插件系统/驱动注册初始化
    void connectExtensionsPanel();   // ActivityBar + 迷你市场面板接线 + 市场页创建
    void connectDataPipeline();      // 模拟器/设备管理器/回放器/录制器 → 壳槽
    void connectSidePanels();        // 侧边栏面板/右侧面板/编辑区/底部面板接线
    void connectProjectPanel();      // 工程面板接线 + 上次工程加载 + 默认实例兜底

    void createMenuBar();
    void createLayout();
    void createStatusBar();
    void createWindowButtons();
    void createCommandCenter();
    void repositionCommandCenter();
    void showCommandPalette(const QString &initialQuery = QString());
    void updateActions();
    void updateStatistics();
    // setupTraceTab 已随 Trace 页迁入 TraceModule（拆分方案 B5：
    // 装配/过滤接线在模块内完成，跨模块编排经 shellInvoke 回调壳）
    // setupSendTab/setupPlaybackTab/setupOfflineAnalysisTab/setupRecordTab
    // 已迁入 TransceiveModule（拆分方案 B2：模块自己连接自己的信号槽）
    // setupDeviceTab/setupMeasurementTab 已迁入 FlowModule（拆分方案 B4）
    void processCommand(const QString &cmd);
    void openTab(QWidget *widget, const QString &label);

    // 实例标签页识别：实例标题本地化为 帧列表/时序波形（2026-08-23 截图反馈：
    // 与侧栏模板名保持一致），旧工程保存的 Trace%1/Graphic%1 标题仍需兼容
    static bool isTraceTabText(const QString &text)
    {
        return text.contains(QStringLiteral("Trace")) ||
               text.contains(QStringLiteral("帧列表"));
    }
    static bool isGraphicTabText(const QString &text)
    {
        return text.contains(QStringLiteral("Graphic")) ||
               text.contains(QStringLiteral("时序波形"));
    }

    void refreshPanelLists();
    void setupMarketTab();  // 创建/重建插件市场页（经 ModuleRegistry "market" 模块，方案 §13 / 拆分方案 B0）
    void marketInvoke(const QString &action, const QVariant &arg = {});  // 市场模块动作转发（invoke 字符串约定见 imodule.h）
    // linkGraphicCursor 已随 Graphic 页迁入 GraphicModule（拆分方案 B5：
    // 视图间游标联动在模块 createPage 内逐对互连）
    QWidget *createTraceInstance(const QString &id);    // 经 trace 模块创建实例 + openTab + flow 注册（B5）
    QWidget *createGraphicInstance(const QString &id);  // 经 graphic 模块创建实例 + openTab + flow 注册（B5）
    QWidget *resolveGraphicTarget();                    // 当前或最后一个 Graphic 视图，无则新建（B5）
    QVariantMap buildSignalMap(quint32 canId, bool extended, const QString &name,
                               const DbcSignal &sig);   // GraphicView::Signal 的跨模块序列化（B5）

    // ---- transceive 模块（拆分方案 B2）----
    ShellContext makeShellContext();           // 构造含数据层服务指针与壳回调的上下文
    void transceiveInvoke(const QString &action, const QVariant &arg = {});  // 收发模块动作转发
    QVariant transceiveQuery(const QString &what, const QVariant &arg = {}); // 收发模块查询转发

    // ---- trace / graphic 模块（拆分方案 B5）----
    void traceInvoke(const QString &action, const QVariant &arg = {});   // trace 模块动作转发
    QVariant traceQuery(const QString &what, const QVariant &arg = {});  // trace 模块查询转发
    void graphicInvoke(const QString &action, const QVariant &arg = {}); // graphic 模块动作转发
    QVariant graphicQuery(const QString &what, const QVariant &arg = {}); // graphic 模块查询转发

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
    // m_filterPresets 已随 Trace 页迁入 TraceModule（拆分方案 B5：模块自持预设管理器）
    BookmarkManager *m_bookmarkMgr = nullptr;
    // m_dataWindow 已随 Graphic 页迁入 GraphicModule（拆分方案 B5：单实例缓存在模块内）
    IOGraphView *m_ioGraph = nullptr;
    WatcherView *m_watcherView = nullptr;  // Watcher 观测页（壳侧单实例，关闭销毁置空）

    // ---- 插件系统 ----
    PluginManager *m_pluginManager = nullptr;

    // ---- UI (Main tabs) ----
    // m_traceTab/m_graphicView 已随 Trace/Graphic 页迁入业务 DLL（拆分方案 B5）；
    // 实例表见下方 m_traceInstances/m_graphicInstances（id → QWidget*）
    // m_sendTab/m_playbackTab/m_offlineTab/m_recordTab 随收发四页迁入
    // openbus_transceive.dll（拆分方案 B2；壳经 createPage/invoke/query 操控）
    // m_deviceTab/m_setupView 随 Flow 页迁入 openbus_flow.dll（拆分方案 B4；页面单实例缓存在模块内）
    QWidget *m_marketWidget = nullptr;   // 统一插件市场（经 ModuleRegistry "market" 模块创建，方案 §13 / 拆分方案 B0）
    SettingsPage *m_settingsPage = nullptr;    // 设置标签页（单实例；侧栏设置条目不再弹窗）
    ShortcutsPage *m_shortcutsPage = nullptr;  // 快捷键参考标签页（单实例）
    WelcomePage *m_welcomePage = nullptr;      // Welcome start page (single instance)

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
    /// Offline file analysis: CaptureLog/SampleStore already hold the full file
    /// (file timestamps). Player ticks only advance progress — do not re-ingest.
    bool m_offlineBulkIngested = false;
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
    // Title-bar layout toggles (VS Code: left / panel / right)
    QToolButton *m_layoutLeftBtn = nullptr;
    QToolButton *m_layoutBottomBtn = nullptr;
    QToolButton *m_layoutRightBtn = nullptr;
    QLabel *m_brandMark = nullptr;
    // VS Code Command Center (menu-bar search pill + palette)
    CommandCenter *m_commandCenter = nullptr;
    CommandPalette *m_commandPalette = nullptr;
    void syncLayoutToggleButtons();
    QPoint m_dragPosition;
};

#endif // MAINWINDOW_H
