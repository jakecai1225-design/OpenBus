#include "busstatistics.h"
#include <cmath>
#include <algorithm>

BusStatistics::BusStatistics(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &BusStatistics::onTimeout);
    m_timer.start();
}

void BusStatistics::onFrame(const CanFrame &frame)
{
    quint32 rawId = frame.id & 0x1FFFFFFF;

    // 全局时间范围
    if (!m_hasFirstFrame) {
        m_globalFirstTime = frame.timestamp;
        m_hasFirstFrame = true;
    }
    m_globalLastTime = frame.timestamp;

    // 错误帧分类
    if (frame.isErrorFrame()) {
        // CAN 错误帧的 ECC 错误码存储在 data[3] 中（SocketCAN 风格）
        // 位定义: bit2-0 = error type (stuff/form/ack/bit0/bit1/crc)
        // 这里使用 frame.data 来判断错误类型
        quint8 ecc = 0;
        if (frame.data.size() >= 4)
            ecc = static_cast<quint8>(frame.data[3]) & 0x07;

        switch (ecc) {
            case 0: m_errorCounts[ErrBit0]++; break;
            case 1: m_errorCounts[ErrBit1]++; break;
            case 2: m_errorCounts[ErrStuff]++; break;
            case 3: m_errorCounts[ErrForm]++; break;
            case 4: m_errorCounts[ErrAck]++; break;
            case 5: m_errorCounts[ErrCrc]++; break;
            default: break;
        }
        m_summary.errorFrames++;
    }

    // ID 级别统计
    auto &data = m_idData[rawId];
    data.frameCount++;
    data.totalBytes += frame.length();

    if (!data.hasFirst) {
        data.firstTimestamp = frame.timestamp;
        data.lastTimestamp = frame.timestamp;
        data.hasFirst = true;
    } else {
        double period = (frame.timestamp - data.lastTimestamp) * 1000.0;  // ms
        if (period > 0 && period < 1e6) {  // 合理性检查
            data.periods.append(period);
            data.periodSum += period;
            if (period < data.minPeriod) data.minPeriod = period;
            if (period > data.maxPeriod) data.maxPeriod = period;
            // 保留最近 1000 个周期值用于抖动计算
            if (data.periods.size() > 1000)
                data.periods.removeFirst();
        }
        data.lastTimestamp = frame.timestamp;
    }

    m_summary.totalFrames++;
}

void BusStatistics::clear()
{
    m_idData.clear();
    for (int i = 0; i < ErrCount; ++i)
        m_errorCounts[i] = 0;
    m_summary = Summary{};
    m_lastIdStats.clear();     // 快照同步归零（Watcher 清零后立即显示 0）
    m_lastSummary = Summary{};
    m_hasFirstFrame = false;
    m_globalFirstTime = 0.0;
    m_globalLastTime = 0.0;
}

void BusStatistics::onTimeout()
{
    computeStats();
}

void BusStatistics::computeStats()
{
    QVector<IdStats> idStats;
    idStats.reserve(m_idData.size());

    for (auto it = m_idData.constBegin(); it != m_idData.constEnd(); ++it) {
        const auto &data = it.value();
        IdStats stats;
        stats.canId = it.key();
        stats.frameCount = data.frameCount;
        stats.totalBytes = data.totalBytes;
        stats.isPeriodic = data.frameCount > 3 && !data.periods.isEmpty();

        if (stats.isPeriodic) {
            double avg = data.periodSum / data.periods.size();
            stats.avgPeriod = avg;
            stats.minPeriod = data.minPeriod;
            stats.maxPeriod = data.maxPeriod;

            // 抖动 σ = sqrt(Σ(xi - x̄)² / N)
            double variance = 0.0;
            for (double p : data.periods)
                variance += (p - avg) * (p - avg);
            variance /= data.periods.size();
            stats.jitter = std::sqrt(variance);

            // 频率 (Hz) = 1000 / avgPeriod(ms)
            if (avg > 0)
                stats.frequency = 1000.0 / avg;
        } else {
            // 事件帧 — 计算总持续时间内的频率
            double duration = data.lastTimestamp - data.firstTimestamp;
            if (duration > 0)
                stats.frequency = data.frameCount / duration;
            stats.avgPeriod = 0;
            stats.minPeriod = 0;
            stats.maxPeriod = 0;
            stats.jitter = 0;
        }

        idStats.append(stats);
    }

    // 按 CAN ID 排序
    std::sort(idStats.begin(), idStats.end(),
              [](const IdStats &a, const IdStats &b) { return a.canId < b.canId; });

    // 汇总
    Summary summary = m_summary;
    summary.uniqueIds = m_idData.size();
    summary.duration = m_hasFirstFrame ? (m_globalLastTime - m_globalFirstTime) : 0.0;

    // 总线负载率 = (总数据位数 / (统计时长 × 波特率)) × 100%
    if (summary.duration > 0 && m_bitrate > 0) {
        quint64 totalBits = 0;
        for (const auto &data : m_idData) {
            // 经典 CAN 帧位数 ≈ 47 + DLC×8（含 SOF/仲裁/CRC/ACK/EOF 等）
            int frameBits = 47 + data.totalBytes * 8;
            // CAN FD 帧额外位数（近似）
            // 简化处理：按经典 CAN 计算
            totalBits += frameBits * data.frameCount;
        }
        summary.busLoadPercent = (totalBits / (summary.duration * m_bitrate)) * 100.0;
    }

    // 快照留存（Watcher 轮询；信号路径行为不变）
    m_lastIdStats = idStats;
    m_lastSummary = summary;

    emit statisticsUpdated(idStats, summary, m_errorCounts);
}

void BusStatistics::lastErrorCounts(quint64 out[ErrCount]) const
{
    if (!out)
        return;
    for (int i = 0; i < ErrCount; ++i)
        out[i] = m_errorCounts[i];
}
