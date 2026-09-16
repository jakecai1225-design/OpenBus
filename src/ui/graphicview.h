#ifndef GRAPHICVIEW_H
#define GRAPHICVIEW_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QHash>
#include <QTimer>
#include "core/canframe.h"
#include "core/dbcdata.h"
#include "core/samplestore.h"
#include "graphic/downsample.h"

class DbcManager;
class DbcSignalPickerDialog;

class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QCustomPlot;
class QCPGraph;
class QCPAxis;
class QCPAxisRect;
class QCPRange;
class QCPItemStraightLine;
class QCPItemText;
class CursorHandleItem;
class QToolBar;
class QToolButton;
class QLabel;
class QCheckBox;
class QComboBox;
class QRubberBand;

/// Graphic 配色（随主题切换：浅色 = CANoe 经典白底基准，深色 = 同构映射）
struct GraphicPalette {
    QColor canvas, grid, trackSep, axis, axisText;
    QColor timeLine, cursor1, cursor2, trackCursor;
    QColor nameTagBg, nameTagFg, nameTagBorder;
    QColor dimCurve;      ///< 聚焦模式未选中曲线的置灰色
    static GraphicPalette canoeLight();
    static GraphicPalette canoeDark();
};

/**
 * @brief CANoe 风格 Graphic 信号图形视图 — 基于 QCustomPlot
 *
 * 布局：
 *   ┌─────────────────────────────────────────────┐
 *   │ 工具栏: 缩放 | 适应 | 采样点 | 单卡尺 | 双卡尺 | 清除 │
 *   ├──────────┬──────────────────────────────────┤
 *   │ 信号列表  │  波形区 (分栏多轴 或 叠加单图)   │
 *   │ 色块/名称 │  ┌─ Signal A (独立 Y 轴) ──┐    │
 *   │ 物理值    │  ├─ Signal B (独立 Y 轴) ──┤    │
 *   │ 原始值    │  ├─ Signal C (独立 Y 轴) ──┤    │
 *   │ 单位      │  └── 共享 X 轴 (仅底部刻度) ┘    │
 *   │          │     卡尺线 (可拖动+手柄标签)      │
 *   │          │     当前时间指示线 (实时)        │
 *   └──────────┴──────────────────────────────────┘
 *
 * 数据架构：原始数据存环形缓冲 (rawData, 百万点)，显示数据按视口
 * min/max 抽稀（O(视口宽) 恒定成本）；卡尺测量始终对原始数据插值。
 *
 * 交互对标 CANoe（详见 doc/Graphic模块设计文档.md §8-§9）：
 *   - Y 轴三模式：分栏 / 叠加·选中轴 / 叠加·全部轴
 *   - 缩放轴三模式：X / Y / XY；框选缩放（扁平框仅 X）；中键平移
 *   - 缩放历史栈（Undo Zoom / Undo All）；快捷键 F/Space/Ctrl+Z/C/V/Esc
 *   - 聚焦三态：全部彩色 / 选中彩色 / 仅显示选中
 */
class GraphicView : public QWidget
{
    Q_OBJECT

public:
    /// 信号配置
    struct Signal {
        QString name;
        quint32 canId = 0;
        bool extended = false;
        QColor color;
        DbcSignal dbcSig;
        int displayMode = 1;   ///< DisplayMode 枚举值（保持可序列化）
    };

    /// 卡尺模式
    enum class CursorMode {
        None,       ///< 无卡尺
        Single,     ///< 单卡尺
        Double      ///< 双卡尺 (显示 Δ 差值)
    };

    /// 曲线显示模式（折线/阶梯/仅点）
    enum class DisplayMode { Linear = 0, Step = 1, Points = 2 };

    /// Y 轴显示方式（对标 CANoe 三态）
    enum class YAxisMode { Separate, OverlaySelected, OverlayAll };

    /// 缩放轴模式（对标 CANoe X/Y/XY 独立缩放）
    enum class ZoomAxisMode { XOnly, YOnly, XY };

    /// 聚焦显示模式（对标 CANoe 全部彩色/选中彩色/仅选中显示）
    enum class FocusMode { AllColor, SelectedColor, SelectedOnly };

    explicit GraphicView(QWidget *parent = nullptr);
    ~GraphicView() override;

    /// 添加信号；history 非空时回填历史帧前缀（离线回放场景：添加即
    /// 显示到当前进度的完整曲线，仅写本信号、不影响既有信号；
    /// historyCount < 0 表示全部，否则取前 historyCount 帧）
    void addSignal(const Signal &sig,
                   const QVector<CanFrame> *history = nullptr,
                   int historyCount = -1);
    void removeSignal(int index);
    /// Remove selected signal-list rows; returns how many were removed
    int removeSelectedSignals();
    void clearSignals();
    QVector<Signal> signalConfigs() const;
    /// 查询第 index 个信号的显示数据点数（视口抽稀后；越界返回 -1）。
    /// 只读诊断接口：offscreen 回归用例以此断言“波形可见”（0 = 无波形）
    int displayedPointCount(int index) const;
    /// 查询第 index 个信号的原始数据点数（环形缓冲；越界返回 -1）。
    /// 只读诊断接口：offscreen 回归用例精确断言回填帧数
    int rawSampleCount(int index) const;
    /// 批量加载信号配置（清除原有后添加）
    void loadSignalConfigs(const QVector<Signal> &configs);

    void setTimeWindow(double seconds) { m_timeWindow = seconds; refreshTimeAxis(); }
    double timeWindow() const { return m_timeWindow; }

    /// M1 预埋：协议 / 形态身份（doc/flow.md §十三）——多协议就绪前恒为 can/waveform
    QString protocolId() const { return m_protocolId; }
    void setProtocolId(const QString &id) { m_protocolId = id; }
    QString formId() const { return m_formId; }
    void setFormId(const QString &id) { m_formId = id; }

    QSize minimumSizeHint() const override { return {400, 200}; }
    QSize sizeHint() const override { return {800, 400}; }

public slots:
    void onFrame(const CanFrame &frame);
    void onFrames(const QVector<CanFrame> &frames);
    void clearData();

    /// 从外部文件加载帧数据（BLF/ASC/CSV）
    void loadFile(const QString &path);

    /// 响应其它视图的游标联动同步（带防回环标志）
    void onSyncCursor(int which, double time);

    /// 设置 DBC 管理器引用（P1）
    void setDbcManager(DbcManager *mgr);

signals:
    /// 文件拖放后加载完成
    void fileLoaded(int frameCount);

    /// Cursor moved (which: 1/2) — multi-view cursor link
    void cursorMoved(int which, double time);

    /// Compact status line for the shell status bar (Signals / Samples / …)
    void statusInfoChanged(const QString &text);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onSamplePullTimer();

private:
    struct SignalData {
        Signal config;
        SampleKey storeKey;
        quint64 sampleSeq = 0;          ///< SampleStore cursor (exclusive)
        bool storeSubscribed = false;
        QCPGraph *graph = nullptr;
        QCPAxis *yAxis = nullptr;
        QCPAxis *overlayYAxis = nullptr;
        QCPAxisRect *axisRect = nullptr;
        QCPItemText *nameLabel = nullptr;
        double dataMin = 0.0;
        double dataMax = 0.0;
        bool hasMinMax = false;
        bool minMaxDirty = false;
        bool userHidden = false;
        /// Local ring only when not subscribed to SampleStore (offline twin path).
        /// Store-backed signals use LOD + valueAtTime — no per-view raw twin (P1-1).
        RingBuffer<graphic::Sample> rawData{65536};
        double cachedT1 = 0.0;
        double cachedT2 = -1.0;
        bool cachedShowPoints = false;
        bool cacheValid = false;
        bool displayDirty = true;
    };

    /// 缩放历史栈条目（按信号索引存 Y 范围，避免轴对象生命周期问题）
    struct ZoomState {
        double x1 = 0.0, x2 = 0.0;
        struct YR { int sig; double lo, hi; };
        QVector<YR> yRanges;
    };

    // --- UI ---
    QSplitter *m_splitter = nullptr;
    QTreeWidget *m_signalTree = nullptr;
    QCustomPlot *m_plot = nullptr;
    QToolBar *m_toolbar = nullptr;
    QLabel *m_cursorInfoLabel = nullptr;   ///< 卡尺信息面板 (ΔT/ΔY/frequency)
    QCheckBox *m_pointsToggle = nullptr;   ///< 采样点显示开关
    QComboBox *m_timeWindowCombo = nullptr; ///< 时间窗口选择
    QToolButton *m_pauseBtn = nullptr;     ///< 暂停/继续
    QLabel *m_statusLabel = nullptr;       ///< 底部状态栏
    QCheckBox *m_cursorLinkToggle = nullptr; ///< 多视图游标联动开关

    // --- 工具栏按钮 ---
    QToolButton *m_cursorSingleBtn = nullptr;
    QToolButton *m_cursorDoubleBtn = nullptr;
    QToolButton *m_cursorClearBtn = nullptr;
    QToolButton *m_zoomInBtn = nullptr;
    QToolButton *m_zoomOutBtn = nullptr;
    QToolButton *m_fitBtn = nullptr;
    QToolButton *m_undoZoomBtn = nullptr;    ///< 撤销缩放 (Ctrl+Z)
    QToolButton *m_rubberZoomBtn = nullptr;  ///< 框选缩放模式开关
    QToolButton *m_panBtn = nullptr;         ///< 手形拖动 (Pan mode)

    // --- 工具栏下拉 ---
    QComboBox *m_displayModeCombo = nullptr; ///< 线型：折线/阶梯/仅点
    QComboBox *m_focusCombo = nullptr;       ///< 聚焦：全部彩色/选中彩色/仅显示选中
    QComboBox *m_yAxisModeCombo = nullptr;   ///< Y 轴：分栏/叠加·选中轴/叠加·全部轴
    QComboBox *m_zoomAxisCombo = nullptr;    ///< 缩放轴：X/Y/XY

    bool m_panMode = false;                 ///< 手形拖动模式（左键平移，与框选互斥）

    // --- 信号数据 ---
    QVector<SignalData> m_signals;
    double m_timeWindow = 30.0;
    double m_currentTime = 0.0;

    // --- 性能节流 ---
    QTimer m_replotTimer;
    bool m_replotPending = false;
    QTimer m_valueTimer;
    static constexpr int VALUE_UPDATE_MS = 200;
    int m_replotIntervalMs = 33;
    int m_rawMaxCapacity = 200000;

    QHash<quint64, QVector<int>> m_idIndex;

    // Phase B3: pull decoded samples from SampleStore (not per-view decode)
    QTimer m_samplePullTimer;

    // --- 视口降采样 ---
    bool m_dataDirty = false;                          ///< 原始数据有更新，需重建显示数据
    graphic::Strategy m_dsStrategy = graphic::Strategy::MinMax;  ///< 抽稀策略

    // --- 采样点 ---
    bool m_showPoints = false;

    // --- 暂停 ---
    bool m_paused = false;

    // --- G7 配色与主题 ---
    GraphicPalette m_palette;

    // --- G7 显示/Y 轴/缩放轴/聚焦 模式 ---
    DisplayMode m_displayMode = DisplayMode::Step;  ///< 新信号默认线型
    YAxisMode m_yAxisMode = YAxisMode::OverlaySelected;  ///< P1-3: overlay default
    bool m_yAxisModeUserLocked = false;  ///< user changed Y-mode combo; skip auto-overlay
    ZoomAxisMode m_zoomAxis = ZoomAxisMode::XY;
    FocusMode m_focusMode = FocusMode::AllColor;
    int m_selectedSignal = -1;                       ///< 信号列表当前选中行

    // --- G7 叠加模式 ---
    QCPAxisRect *m_overlayRect = nullptr;
    QCPAxis *m_overlayXAxis = nullptr;

    // --- G7 缩放历史栈 ---
    QVector<ZoomState> m_zoomStack;
    QTimer m_zoomPushTimer;            ///< 滚轮缩放防抖 push
    bool m_restoringZoom = false;      ///< 恢复中不再 push

    // --- G7 框选缩放 / 中键平移 ---
    QRubberBand *m_rubberBand = nullptr;
    QPoint m_rubberOrigin;
    bool m_rubberZoom = true;          ///< 框选缩放模式开关（默认开，对标 CANoe）
    bool m_panning = false;            ///< 中键平移中
    QPoint m_panStartPos;

    // --- G14 P1: DBC 管理器引用（用于信号选择对话框） ----
    DbcManager *m_dbcManager = nullptr;
    double m_panStartX1 = 0.0, m_panStartX2 = 0.0;
    struct PanY { int sig; double lo, hi; };
    QVector<PanY> m_panStartY;

    // ---- G15 P3/P4: 信号列表列配置 ----
    QHash<int, bool> m_columnVisibility;     ///< 列号 → 可见性
    QHash<int, int> m_columnWidths;          ///< 列号 → 用户自定义宽度

    // --- G8 轴区交互（§十：轴区独立缩放/平移 + 时间窗箭头） ---
    enum class AxisDragMode { None, X, Y };
    AxisDragMode m_axisDrag = AxisDragMode::None;   ///< 轴区拖动中（X 轴区=平移时间，Y 轴区=平移该轴）
    int m_axisDragSig = -1;                         ///< Y 拖动目标信号（叠加并排轴定位；-1 = 全部）
    QToolButton *m_timeBackBtn = nullptr;           ///< 时间窗后移（chevron-left，按住连续）
    QToolButton *m_timeFwdBtn = nullptr;            ///< 时间窗前移（chevron-right）
    QToolButton *m_yUpBtn = nullptr;                ///< 选中信号 Y 轴上移（chevron-up，无选中时禁用）
    QToolButton *m_yDownBtn = nullptr;              ///< 选中信号 Y 轴下移（chevron-down）

    // --- 当前时间指示线 ---
    QCPItemStraightLine *m_currentTimeLine = nullptr;

    // --- G7 鼠标跟踪线 ---
    QCPItemStraightLine *m_trackLine = nullptr;
    QCPItemText *m_trackLabel = nullptr;

    // --- 卡尺 ---
    CursorMode m_cursorMode = CursorMode::None;
    QCPItemStraightLine *m_cursor1 = nullptr;
    QCPItemStraightLine *m_cursor2 = nullptr;
    CursorHandleItem *m_cursor1Handle = nullptr;   ///< 卡尺顶部手柄（SVG 下箭头）
    CursorHandleItem *m_cursor2Handle = nullptr;
    QCPItemText *m_cursor1Label = nullptr;    ///< 卡尺时间标签
    QCPItemText *m_cursor2Label = nullptr;
    int m_draggingCursor = 0;   ///< 0=none, 1=cursor1, 2=cursor2
    double m_cursor1Time = 0.0;
    double m_cursor2Time = 0.0;
    bool m_cursor1Placed = false;  ///< Single/double: C1 visible after plot click
    bool m_cursor2Placed = false;  ///< Double: C2 visible after second plot click
    bool m_syncingCursor = false;  ///< 正在同步游标（防回环）
    bool m_cursorLink = true;      ///< 多视图游标联动开关

    // --- 内部方法 ---

    /// 从帧数据中提取信号物理值
    double extractValue(const CanFrame &frame, const Signal &sig) const;

    /// 从帧数据中提取信号原始值
    quint64 extractRaw(const CanFrame &frame, const Signal &sig) const;

    /// 自动分配颜色（CANoe 经典高饱和 16 色板）
    static QColor autoColor(int index);

    /// 构建 UI
    void setupUi();

    /// 主题（浅色 = CANoe 白底基准）
    static bool isLightTheme();
    QString toolbarQss() const;
    QString treeQss() const;
    QString infoLabelQss() const;

    /// 应用当前主题配色到全部元素（画布/轴/网格/标签/QSS）
    void applyPalette();
    void updateToolbarIcons();   ///< 主题切换后重刷工具栏图标颜色

    /// 配置单个 axisRect 的样式（palette 化；X 刻度仅末轨道由布局控制）
    void styleAxisRect(QCPAxisRect *ar);
    void styleYAxis(QCPAxis *axis);

    /// 当前生效的取值轴（分栏 = sd.yAxis；叠加 = sd.overlayYAxis）
    QCPAxis *valueAxisFor(const SignalData &sd) const;

    /// 主 X 轴（分栏 = 首个可见轨道 X 轴；叠加 = overlay X 轴）
    QCPAxis *primaryXAxis() const;

    /// 首个可见 axisRect（item 手柄定位用）
    QCPAxisRect *primaryRect() const;

    /// 统一设置全部轨道 X 范围（blocker + 显示数据重建）
    void setXRangeAll(const QCPRange &range, bool refresh = true);

    /// X 轴联动信号连接（rangeChanged → 同步 + 抽稀重建）
    void connectXAxis(QCPAxis *xAxis);

    /// 刷新信号名叠加标签（文本/颜色/底色，随主题与聚焦状态）
    void refreshNameLabels();

    /// 叠加模式 Y 轴可见性/并排偏移（OverlayAll 并排 · OverlaySelected 仅选中轴）
    void applyOverlayAxisVisibility();

    /// 刷新信号列表（结构）
    void updateSignalList();

    /// 反选信号列表（G13 多选操作）
    void invertSignalSelection();

    /// 更新信号列表中的实时值
    void updateSignalValues();

    /// 更新信号列表中的卡尺值
    void updateCursorValues();

    /// 刷新时间轴范围（所有 axis rect 同步）
    void refreshTimeAxis();

    /// 布局多轴 axisRect（分栏模式：每信号一行；X 刻度仅底部）
    void layoutAxisRects();

    /// Y 轴三模式切换（分栏 / 叠加·选中轴 / 叠加·全部轴）
    void applyYAxisMode();
    /// B7: auto-switch to overlay when signal count crosses threshold.
    void maybeAutoOverlay();
    void buildOverlay();
    void teardownOverlay();

    /// 曲线显示模式应用（单信号 / 全部）
    void applyDisplayMode(SignalData &sd);
    void applyDisplayModeAll();

    /// 聚焦三态应用（全部彩色 / 选中彩色 / 仅显示选中）
    void applyFocus();

    /// 选中信号变化（叠加·选中轴模式的刻度切换 + 聚焦刷新）
    void setSelectedSignal(int index);

    /// 缩放轴模式
    void setZoomAxisMode(ZoomAxisMode m);
    /// 以视口中心（或指定像素位置）为基准缩放，受缩放轴模式约束
    void zoomAt(double factor, const QPointF &plotPos);

    /// Zoom axis range about @p anchor (data coord under the cursor stays fixed).
    static QCPRange zoomRangeAbout(const QCPRange &r, double anchor, double factor);

    /// 缩放历史栈
    void pushZoomState();
    void undoZoom();
    void undoAllZooms();
    void restoreZoomState(const ZoomState &st);
    void updateZoomUi();
    ZoomState currentZoomState() const;

    /// 单信号 Y 操作（对标 CANoe 轴快捷操作）
    void fitSignalY(int index);
    void resetSignalYToDbc(int index);
    void showAxisConfigDialog(int index);

    /// 波形区位置 → 信号索引 / Y 刻度区命中判断
    int signalIndexAtPos(const QPoint &pos) const;
    int signalIndexForAxis(QCPAxis *yAxis) const;

    /// 轴区命中（§十）：0=非轴区 1=X轴区 2=Y轴区；outIdx = Y 轴区对应信号（-1 = 叠加全部）
    int axisZoneAt(const QPoint &pos, int *outIdx = nullptr) const;

    /// 时间窗平移（frac = 视口宽比例；连续调用合并为一级缩放历史）
    void shiftTimeAxis(double frac);

    /// 选中信号 Y 轴平移（frac = 视口高比例；无选中时 no-op）
    void shiftYAxis(double frac);

    /// Create/get cursor lines (handles + time labels)
    void ensureCursors();

    /// Union of all visible track rects (full-height cursor / track line span)
    QRect cursorSpanRect() const;

    /// CANoe-style click: place unplaced cursor(s), else move nearest / drag hit
    void placeOrMoveCursorAt(const QPoint &pos);

    /// 创建/获取当前时间指示线
    void ensureCurrentTimeLine();

    /// 创建/获取鼠标跟踪线
    void ensureTrackLine();

    // ---- G15 P3/P4: 信号列表列配置管理 ----
    void showColumnVisibilityMenu(const QPoint &pos);
    void restoreColumnConfig();
    void saveColumnConfig();

    /// 卡尺手柄/标签随光标位置与视口更新
    void updateCursorDecorations();

    /// 设置卡尺模式
    void setCursorMode(CursorMode mode);

    /// 移动卡尺到指定时间
    void moveCursor(int which, double time);

    /// 对原始数据在指定时间插值取值（卡尺测量基准，不受降采样影响）
    static bool valueAtTime(const RingBuffer<graphic::Sample> &raw, double time, double &outVal);

    /// 原始数据入环形缓冲（覆盖最旧时标记 min/max 重算）
    void pushSample(SignalData &sd, double t, double v);
    void rebuildIdIndex();

    /// Subscribe signal to SampleStore; optional history backfill into the store.
    void subscribeStore(SignalData &sd, const QVector<CanFrame> *history, int historyCount);
    void unsubscribeStore(SignalData &sd);
    /// Pull new samples from SampleStore into local display rings.
    void pullFromSampleStore();
    void updateSamplePullBudget();

    /// min/max 失效时重算（供 Y 轴自适应/列表显示）
    static void ensureMinMax(SignalData &sd);

    /// 按当前视口对全部信号重建显示数据（min/max 抽稀 + 视口缓存）
    void refreshDisplayData();

    /// 定时批量重绘
    void onReplotTimeout();

    /// 适应窗口：重缩放所有轴
    void fitAll();

    /// X 轴自适应：时间范围适配全部数据（G13）
    void fitXOnly();

    /// Y 轴自适应：全部信号 Y 轴适配数据范围（G13）
    void fitYOnly();

    /// 导出图表为图片
    void exportPlot();

    /// 格式化时间 (mm:ss.ms)
    static QString formatTime(double seconds);

    /// 更新底部状态栏
    void updateStatusBar();

    // ---- M1 预埋：协议 / 形态身份（doc/flow.md §十三）----
    QString m_protocolId = QStringLiteral("can");
    QString m_formId = QStringLiteral("waveform");
};

#endif // GRAPHICVIEW_H
