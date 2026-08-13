#ifndef GRAPHICVIEW_H
#define GRAPHICVIEW_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QTimer>
#include "core/canframe.h"
#include "core/dbcdata.h"

class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QCustomPlot;
class QCPGraph;
class QCPAxis;
class QCPAxisRect;
class QCPItemStraightLine;
class QCPItemText;
class QToolBar;
class QToolButton;
class QLabel;
class QCheckBox;
class QComboBox;

/**
 * @brief CANoe 风格 Graphic 信号图形视图 — 基于 QCustomPlot
 *
 * 布局：
 *   ┌─────────────────────────────────────────────┐
 *   │ 工具栏: 缩放 | 适应 | 采样点 | 单卡尺 | 双卡尺 | 清除 │
 *   ├──────────┬──────────────────────────────────┤
 *   │ 信号列表  │  波形区 (多轴垂直堆叠)           │
 *   │ 名称      │  ┌─ Signal A (独立 Y 轴) ──┐    │
 *   │ 原始值    │  ├─ Signal B (独立 Y 轴) ──┤    │
 *   │ 物理值    │  ├─ Signal C (独立 Y 轴) ──┤    │
 *   │ 单位      │  └── 共享 X 轴 (时间) ──────┘    │
 *   │          │     卡尺线 (可拖动)              │
 *   │          │     当前时间指示线 (实时)        │
 *   └──────────┴──────────────────────────────────┘
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
    };

    /// 卡尺模式
    enum class CursorMode {
        None,       ///< 无卡尺
        Single,     ///< 单卡尺
        Double      ///< 双卡尺 (显示 Δ 差值)
    };

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

signals:
    /// 文件拖放后加载完成
    void fileLoaded(int frameCount);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct SignalData {
        Signal config;
        QCPGraph *graph = nullptr;
        QCPAxis *yAxis = nullptr;
        QCPAxisRect *axisRect = nullptr;
        QCPItemText *nameLabel = nullptr;  ///< 信号名叠加文本
        double dataMin = 0.0;             ///< 数据最小值
        double dataMax = 0.0;             ///< 数据最大值
        bool hasMinMax = false;           ///< 是否已计算 min/max
    };

    // --- UI ---
    QSplitter *m_splitter = nullptr;
    QTreeWidget *m_signalTree = nullptr;
    QCustomPlot *m_plot = nullptr;
    QToolBar *m_toolbar = nullptr;
    QLabel *m_cursorInfoLabel = nullptr;  ///< 卡尺信息面板 (ΔT/ΔY/frequency)
    QCheckBox *m_pointsToggle = nullptr;   ///< 采样点显示开关
    QComboBox *m_timeWindowCombo = nullptr; ///< 时间窗口选择
    QToolButton *m_pauseBtn = nullptr;      ///< 暂停/继续
    QLabel *m_statusLabel = nullptr;        ///< 底部状态栏

    // --- 工具栏按钮 ---
    QToolButton *m_cursorSingleBtn = nullptr;
    QToolButton *m_cursorDoubleBtn = nullptr;
    QToolButton *m_cursorClearBtn = nullptr;

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
    static constexpr int MAX_DISPLAY_POINTS = 50000;  ///< 显示上限, 超出则裁剪

    // --- 采样点 ---
    bool m_showPoints = true;

    // --- 暂停 ---
    bool m_paused = false;

    // --- 当前时间指示线 ---
    QCPItemStraightLine *m_currentTimeLine = nullptr;

    // --- 卡尺 ---
    CursorMode m_cursorMode = CursorMode::None;
    QCPItemStraightLine *m_cursor1 = nullptr;
    QCPItemStraightLine *m_cursor2 = nullptr;
    int m_draggingCursor = 0;   ///< 0=none, 1=cursor1, 2=cursor2
    double m_cursor1Time = 0.0;
    double m_cursor2Time = 0.0;

    // --- 内部方法 ---

    /// 从帧数据中提取信号物理值
    double extractValue(const CanFrame &frame, const Signal &sig) const;

    /// 从帧数据中提取信号原始值
    quint64 extractRaw(const CanFrame &frame, const Signal &sig) const;

    /// 自动分配颜色
    static QColor autoColor(int index);

    /// 构建 UI
    void setupUi();

    /// 配置单个 axisRect 的样式 (网格/坐标轴/字体)
    void styleAxisRect(QCPAxisRect *ar, const QColor &color, const QString &name);

    /// 刷新信号列表（结构）
    void updateSignalList();

    /// 更新信号列表中的实时值
    void updateSignalValues();

    /// 更新信号列表中的卡尺值
    void updateCursorValues();

    /// 刷新时间轴范围（所有 axis rect 同步）
    void refreshTimeAxis();

    /// 布局多轴 axisRect（每个信号一行）
    void layoutAxisRects();

    /// 创建/获取卡尺线
    void ensureCursors();

    /// 创建/获取当前时间指示线
    void ensureCurrentTimeLine();

    /// 设置卡尺模式
    void setCursorMode(CursorMode mode);

    /// 移动卡尺到指定时间
    void moveCursor(int which, double time);

    /// 获取 graph 在指定时间附近的值（插值）
    bool valueAtTime(QCPGraph *graph, double time, double &outVal) const;

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
