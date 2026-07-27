#ifndef GRAPHICVIEW_H
#define GRAPHICVIEW_H

#include <QWidget>
#include <QVector>
#include <QColor>
#include "core/canframe.h"

/**
 * @brief CANoe 风格 Graphic 信号图形视图
 *
 * 以 QPainter 绘制信号值随时间变化的折线图。
 * 支持添加多个信号（指定 CAN ID + 字节偏移 + 位长）。
 * 自动滚动时间窗口、Y 轴自适应。
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
        int byteOffset = 0;    ///< 数据字节偏移 (0-based)
        int bitLength = 8;     ///< 8, 16, 32
        bool bigEndian = false;
        QColor color;
    };

    explicit GraphicView(QWidget *parent = nullptr);

    void addSignal(const Signal &sig);
    void removeSignal(int index);
    void clearSignals();
    QVector<Signal> signalConfigs() const;

    void setTimeWindow(double seconds) { m_timeWindow = seconds; update(); }
    double timeWindow() const { return m_timeWindow; }

    QSize minimumSizeHint() const override { return {300, 150}; }
    QSize sizeHint() const override { return {600, 300}; }

public slots:
    void onFrame(const CanFrame &frame);
    void clearData();

protected:
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct DataPoint {
        double time;
        double value;
    };

    struct SignalData {
        Signal config;
        QVector<DataPoint> points;
    };

    QVector<SignalData> m_signals;
    double m_timeWindow = 30.0;  ///< 显示最近 N 秒
    double m_currentTime = 0.0;

    /// 从帧数据中提取信号值
    double extractValue(const CanFrame &frame, const Signal &sig) const;

    /// 绘制网格
    void drawGrid(QPainter &p, const QRectF &plotArea);

    /// 绘制信号折线
    void drawSignal(QPainter &p, const QRectF &plotArea,
                    const SignalData &sig, double minVal, double maxVal);

    /// 绘制图例
    void drawLegend(QPainter &p);

    /// 自动分配颜色
    static QColor autoColor(int index);
};

#endif // GRAPHICVIEW_H
