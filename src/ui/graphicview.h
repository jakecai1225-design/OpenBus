#ifndef GRAPHICVIEW_H
#define GRAPHICVIEW_H

#include <QWidget>
#include <QVector>
#include <QColor>
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
class QToolBar;
class QToolButton;
class QLabel;

/**
 * @brief CANoe 风格 Graphic 信号图形视图 — 基于 QCustomPlot
 *
 * 布局：
 *   ┌─────────────────────────────────────────────┐
 *   │ 工具栏: 缩放 | 适应 | 单卡尺 | 双卡尺 | 清除 │
 *   ├──────────┬──────────────────────────────────┤
 *   │ 信号列表  │  波形区 (多轴垂直堆叠)           │
 *   │ 名称      │  ┌─ Signal A (独立 Y 轴) ──┐    │
 *   │ 原始值    │  ├─ Signal B (独立 Y 轴) ──┤    │
 *   │ 物理值    │  ├─ Signal C (独立 Y 轴) ──┤    │
 *   │ 单位      │  └── 共享 X 轴 (时间) ──────┘    │
 *   │          │     卡尺线 (可拖动)              │
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
    };

    // --- UI ---
    QSplitter *m_splitter = nullptr;
    QTreeWidget *m_signalTree = nullptr;
    QCustomPlot *m_plot = nullptr;
    QToolBar *m_toolbar = nullptr;
    QLabel *m_cursorInfoLabel = nullptr;  ///< 卡尺信息面板 (ΔT/ΔY/frequency)

    // --- 工具栏按钮 ---
    QToolButton *m_cursorSingleBtn = nullptr;
    QToolButton *m_cursorDoubleBtn = nullptr;
    QToolButton *m_cursorClearBtn = nullptr;

    // --- 信号数据 ---
    QVector<SignalData> m_signals;
    double m_timeWindow = 30.0;
    double m_currentTime = 0.0;

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

    /// 刷新信号列表（结构）
    void updateSignalList();

    /// 更新信号列表中的卡尺值
    void updateCursorValues();

    /// 刷新时间轴范围（所有 axis rect 同步）
    void refreshTimeAxis();

    /// 布局多轴 axisRect（每个信号一行）
    void layoutAxisRects();

    /// 创建/获取卡尺线
    void ensureCursors();

    /// 设置卡尺模式
    void setCursorMode(CursorMode mode);

    /// 移动卡尺到指定时间
    void moveCursor(int which, double time);

    /// 获取 graph 在指定时间附近的值（插值）
    bool valueAtTime(QCPGraph *graph, double time, double &outVal) const;
};

#endif // GRAPHICVIEW_H
