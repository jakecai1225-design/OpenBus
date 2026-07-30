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
 * @brief CANoe 椋庢牸 Graphic 淇″彿鍥惧舰瑙嗗浘 鈥?鍩轰簬 Qt Charts
 *
 * 宸︿晶淇″彿鍒楄〃 (鍕鹃€?棰滆壊/鍚嶇О) + 鍙充晶 QChartView 娉㈠舰鍖恒€? * 鏀寔娣诲姞澶氫釜淇″彿 (鎸囧畾 CAN ID + 瀛楄妭鍋忕Щ + 浣嶉暱)銆? * 宸ュ叿鏍忔敮鎸佺缉鏀?閫傚簲/娴嬮噺銆? */
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
