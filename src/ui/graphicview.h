#ifndef GRAPHICVIEW_H
#define GRAPHICVIEW_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include "core/canframe.h"
#include "core/dbcdata.h"

class QSplitter;
class QListWidget;
class QListWidgetItem;
class QChart;
class QChartView;
class QLineSeries;
class QValueAxis;
class QToolBar;

/**
 * @brief CANoe 风格 Graphic 信号图形视图 — 基于 Qt Charts
 *
 * 左侧信号列表 (勾选/颜色/名称) + 右侧 QChartView 波形区。
 * 支持添加多个信号 (指定 CAN ID + 字节偏移 + 位长)。
 * 工具栏支持缩放/适应/测量。
 */
class GraphicView : public QWidget
{
    Q_OBJECT

public:
    /// 信号配置 — 内嵌完整 DBC 信号定义，支持精确解码
    struct Signal {
        QString name;
        quint32 canId = 0;
        bool extended = false;
        QColor color;
        // DBC 信号定义（用于精确解码）
        DbcSignal dbcSig;   ///< 包含 startBit/bitLength/endian/signed/factor/offset 等
    };

    explicit GraphicView(QWidget *parent = nullptr);

    void addSignal(const Signal &sig);
    void removeSignal(int index);
    void clearSignals();
    QVector<Signal> signalConfigs() const;

    void setTimeWindow(double seconds) { m_timeWindow = seconds; refreshTimeAxis(); }
    double timeWindow() const { return m_timeWindow; }

    QSize minimumSizeHint() const override { return {300, 150}; }
    QSize sizeHint() const override { return {600, 300}; }

public slots:
    void onFrame(const CanFrame &frame);
    void clearData();

private:
    struct SignalData {
        Signal config;
        QLineSeries *series = nullptr;
        QValueAxis *yAxis = nullptr;
    };

    QSplitter *m_splitter = nullptr;
    QListWidget *m_signalList = nullptr;
    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QValueAxis *m_timeAxis = nullptr;
    QToolBar *m_toolbar = nullptr;

    QVector<SignalData> m_signals;
    double m_timeWindow = 30.0;  ///< 显示最近 N 秒
    double m_currentTime = 0.0;

    /// 从帧数据中提取信号值
    double extractValue(const CanFrame &frame, const Signal &sig) const;

    /// 自动分配颜色
    static QColor autoColor(int index);

    /// 构建 UI
    void setupUi();

    /// 刷新信号列表
    void updateSignalList();

    /// 刷新时间轴范围
    void refreshTimeAxis();
};

#endif // GRAPHICVIEW_H
