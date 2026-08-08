#ifndef BUSSTATISTICS_H
#define BUSSTATISTICS_H

#include <QObject>
#include <QHash>
#include <QVector>
#include <QTimer>
#include "core/canframe.h"

/**
 * @brief 总线统计分析引擎（对标 CANoe Bus Statistics Window）
 *
 * 功能：
 *   - 按 CAN ID 统计帧数、平均/最小/最大周期、周期抖动(σ)、频率
 *   - 错误帧分类统计（Stuff/Form/ACK/Bit0/Bit1/CRC Error）
 *   - 总线负载率计算
 *
 * 刷新策略：1s 定时器触发统计计算，避免每帧更新 UI。
 */
class BusStatistics : public QObject
{
    Q_OBJECT

public:
    /// 单个 CAN ID 的统计数据
    struct IdStats {
        quint32 canId = 0;
        quint64 frameCount = 0;
        double avgPeriod = 0.0;    ///< 平均周期 (ms)
        double minPeriod = 0.0;    ///< 最小周期 (ms)
        double maxPeriod = 0.0;    ///< 最大周期 (ms)
        double jitter = 0.0;       ///< 周期抖动 σ (ms)
        double frequency = 0.0;    ///< 频率 (Hz)
        quint64 totalBytes = 0;    ///< 累计字节数
        bool isPeriodic = false;   ///< 是否周期报文 (frameCount > 3)
    };

    /// 错误帧分类
    enum ErrorType {
        ErrStuff = 0,
        ErrForm,
        ErrAck,
        ErrBit0,
        ErrBit1,
        ErrCrc,
        ErrCount
    };

    /// 汇总统计
    struct Summary {
        quint64 totalFrames = 0;
        int uniqueIds = 0;
        double busLoadPercent = 0.0;  ///< 总线负载率
        quint64 errorFrames = 0;
        double duration = 0.0;       ///< 统计时长 (s)
    };

    explicit BusStatistics(QObject *parent = nullptr);

    void setBitrate(quint32 bps) { m_bitrate = bps; }

public slots:
    void onFrame(const CanFrame &frame);
    void clear();

signals:
    /// 周期性统计刷新信号（1s 间隔）
    void statisticsUpdated(const QVector<IdStats> &idStats,
                           const Summary &summary,
                           const quint64 errorCounts[ErrCount]);

private slots:
    void onTimeout();

private:
    struct IdData {
        quint64 frameCount = 0;
        double lastTimestamp = 0.0;
        double firstTimestamp = 0.0;
        quint64 totalBytes = 0;
        QVector<double> periods;  ///< 最近 N 个周期值（用于抖动计算）
        double periodSum = 0.0;
        double minPeriod = 1e9;
        double maxPeriod = 0.0;
        bool hasFirst = false;
    };

    QHash<quint32, IdData> m_idData;
    quint64 m_errorCounts[ErrCount] = {};
    Summary m_summary;
    quint32 m_bitrate = 500000;  ///< 默认 500kbps
    QTimer m_timer;
    double m_globalFirstTime = 0.0;
    double m_globalLastTime = 0.0;
    bool m_hasFirstFrame = false;

    void computeStats();
};

#endif // BUSSTATISTICS_H
