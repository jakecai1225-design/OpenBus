#ifndef IOGRAPHVIEW_H
#define IOGRAPHVIEW_H

#include <QWidget>
#include <QVector>
#include <QHash>
#include <QTimer>
#include "core/canframe.h"

class QCustomPlot;
class QCPGraph;
class QComboBox;
class QToolButton;
class QLabel;

/**
 * @brief I/O Graph — 帧率 / 总线负载随时间曲线（对标 Wireshark I/O Graphs）
 *
 * 壳侧在帧热路径上仅在可见时调用 onFrame()；内部按时间桶聚合后用
 * QCustomPlot 绘制。支持全局帧率与按 CAN ID 分组。
 */
class IOGraphView : public QWidget
{
    Q_OBJECT

public:
    explicit IOGraphView(QWidget *parent = nullptr);

public slots:
    void onFrame(const CanFrame &frame);
    void clearData();

private slots:
    void onRefreshTimer();
    void onModeChanged(int index);
    void onClearClicked();

private:
    void rebuildPlot();
    void ensureIdSeries(quint32 canId);

    QCustomPlot *m_plot = nullptr;
    QCPGraph *m_fpsGraph = nullptr;
    QCPGraph *m_loadGraph = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTimer m_refreshTimer;

    /// 1s buckets: start time (s) → frame count
    QHash<qint64, int> m_globalBuckets;
    /// canId → (bucket → count)
    QHash<quint32, QHash<qint64, int>> m_idBuckets;
    /// canId → graph
    QHash<quint32, QCPGraph *> m_idGraphs;

    double m_t0 = -1.0;          ///< first frame timestamp (s)
    double m_lastTs = 0.0;
    qint64 m_totalFrames = 0;
    int m_bitrate = 500000;      ///< assumed bitrate for load% (classic CAN)
    bool m_byId = false;
};

#endif // IOGRAPHVIEW_H
