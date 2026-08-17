#ifndef GRAPHICVIEW_H
#define GRAPHICVIEW_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QTimer>
#include "core/canframe.h"
#include "core/dbcdata.h"
#include "graphic/downsample.h"

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

    void addSignal(const Signal &sig);
    void removeSignal(int index);
    void clearSignals();
    QVector<Signal> signalConfigs() const;
    /// 批量加载信号配置（清除原有后添加）
    void loadSignalConfigs(const QVector<Signal> &configs);

    void setTimeWindow(double seconds) { m_timeWindow = seconds; refreshTimeAxis(); }
    double timeWindow() const { return m_timeWindow; }

    QSize minimumSizeHint() const override { return {400, 200}; }
    QSize sizeHint() const override { return {800, 400}; }

public slots:
    void onFrame(const CanFrame &frame);
    void clearData();

    /// 从外部文件加载帧数据（BLF/ASC/CSV）
    void loadFile(const QString &path);

    /// 响应其它视图的游标联动同步（带防回环标志）
    void onSyncCursor(int which, double time);

signals:
    /// 文件拖放后加载完成
    void fileLoaded(int frameCount);

    /// 卡尺被移动（which: 1/2），用于多视图游标联动
    void cursorMoved(int which, double time);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct SignalData {
        Signal config;
        QCPGraph *graph = nullptr;
        QCPAxis *yAxis = nullptr;           ///< 分栏模式的 Y 轴
        QCPAxis *overlayYAxis = nullptr;    ///< 叠加模式的 Y 轴
        QCPAxisRect *axisRect = nullptr;    ///< 分栏模式的轴区
        QCPItemText *nameLabel = nullptr;   ///< 信号名叠加文本
        double dataMin = 0.0;               ///< 数据最小值
        double dataMax = 0.0;               ///< 数据最大值
        bool hasMinMax = false;             ///< 是否已计算 min/max
        bool minMaxDirty = false;           ///< 环形缓冲覆盖后需重算 min/max
        bool userHidden = false;            ///< 用户通过复选框隐藏
        RingBuffer<graphic::Sample> rawData;  ///< 原始数据（卡尺测量基准）
        double cachedT1 = 0.0;              ///< 显示缓存对应视口左边界
        double cachedT2 = -1.0;             ///< 显示缓存对应视口右边界
        bool cacheValid = false;            ///< 显示缓存有效
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

    // --- 工具栏下拉 ---
    QComboBox *m_displayModeCombo = nullptr; ///< 线型：折线/阶梯/仅点
    QComboBox *m_focusCombo = nullptr;       ///< 聚焦：全部彩色/选中彩色/仅显示选中
    QComboBox *m_yAxisModeCombo = nullptr;   ///< Y 轴：分栏/叠加·选中轴/叠加·全部轴
    QComboBox *m_zoomAxisCombo = nullptr;    ///< 缩放轴：X/Y/XY

    // --- 信号数据 ---
    QVector<SignalData> m_signals;
    double m_timeWindow = 30.0;
    double m_currentTime = 0.0;

    // --- 性能节流 ---
    QTimer m_replotTimer;               ///< 定时批量 replot (50ms = 20fps)
    bool m_replotPending = false;       ///< 有待重绘的数据
    QTimer m_valueTimer;                ///< 定时刷新信号列表值 (200ms)
    static constexpr int REPLOT_INTERVAL_MS = 50;
    static constexpr int VALUE_UPDATE_MS = 200;
    static constexpr int RAW_CAPACITY = 1000000;  ///< 每信号原始数据容量（环形缓冲）

    // --- 视口降采样 ---
    bool m_dataDirty = false;                          ///< 原始数据有更新，需重建显示数据
    graphic::Strategy m_dsStrategy = graphic::Strategy::MinMax;  ///< 抽稀策略

    // --- 采样点 ---
    bool m_showPoints = true;

    // --- 暂停 ---
    bool m_paused = false;

    // --- G7 配色与主题 ---
    GraphicPalette m_palette;

    // --- G7 显示/Y 轴/缩放轴/聚焦 模式 ---
    DisplayMode m_displayMode = DisplayMode::Step;  ///< 新信号默认线型
    YAxisMode m_yAxisMode = YAxisMode::Separate;
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
    double m_panStartX1 = 0.0, m_panStartX2 = 0.0;
    struct PanY { int sig; double lo, hi; };
    QVector<PanY> m_panStartY;

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
    QCPItemText *m_cursor1Handle = nullptr;   ///< 卡尺顶部手柄▼
    QCPItemText *m_cursor2Handle = nullptr;
    QCPItemText *m_cursor1Label = nullptr;    ///< 卡尺时间标签
    QCPItemText *m_cursor2Label = nullptr;
    int m_draggingCursor = 0;   ///< 0=none, 1=cursor1, 2=cursor2
    double m_cursor1Time = 0.0;
    double m_cursor2Time = 0.0;
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

    /// 创建/获取卡尺线（含手柄与时间标签）
    void ensureCursors();

    /// 创建/获取当前时间指示线
    void ensureCurrentTimeLine();

    /// 创建/获取鼠标跟踪线
    void ensureTrackLine();

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

    /// min/max 失效时重算（供 Y 轴自适应/列表显示）
    static void ensureMinMax(SignalData &sd);

    /// 按当前视口对全部信号重建显示数据（min/max 抽稀 + 视口缓存）
    void refreshDisplayData();

    /// 定时批量重绘
    void onReplotTimeout();

    /// 适应窗口：重缩放所有轴
    void fitAll();

    /// 导出图表为图片
    void exportPlot();

    /// 格式化时间 (mm:ss.ms)
    static QString formatTime(double seconds);

    /// 更新底部状态栏
    void updateStatusBar();
};

#endif // GRAPHICVIEW_H
