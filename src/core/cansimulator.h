#ifndef CANSIMULATOR_H
#define CANSIMULATOR_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QVector>
#include <atomic>
#include "core/canframe.h"
#include "utils/message_queue.h"

/**
 * @brief CAN 报文模拟器
 *
 * 无硬件环境下生成模拟 CAN/CAN FD 流量，用于界面演示和测试。
 * 生成多种标准 ID、扩展 ID、经典帧和 FD 帧的混合流量。
 *
 * 架构：后台线程生成帧 → 无锁队列 → 主线程定时批量消费 → emit frameGenerated
 * 这种生产者-消费者模式解耦了帧生成和 UI 处理，适合高频场景。
 */
class CanSimulator : public QObject
{
    Q_OBJECT

public:
    explicit CanSimulator(QObject *parent = nullptr);
    ~CanSimulator();

    bool isRunning() const { return m_running; }
    quint8 channel() const { return m_channel; }
    void setChannel(quint8 ch) { m_channel = ch; }
    int baudrate() const { return m_baudrate; }
    void setBaudrate(int b) { m_baudrate = b; }
    int intervalMs() const { return m_intervalMs; }
    void setIntervalMs(int ms);

    /// 队列中待消费帧数（近似值，用于监控）
    size_t pendingFrames() const { return m_queue.approxSize(); }

public slots:
    void start();
    void stop();

signals:
    /// Single-frame signal (compat / low-rate paths). Prefer framesGenerated on the hot path.
    void frameGenerated(const CanFrame &frame);
    /// Bulk delivery from drainQueue (Phase A: one GUI slot per drain tick).
    void framesGenerated(const QVector<CanFrame> &frames);

private slots:
    void drainQueue();

private:
    std::atomic<bool> m_running{false};
    QTimer m_drainTimer;          ///< 主线程定时器，批量消费队列
    QThread *m_workerThread = nullptr;
    quint8 m_channel = 1;
    int m_baudrate = 500000;      ///< 波特率（工程配置用）
    int m_intervalMs = 5;         ///< 帧生成间隔（毫秒）

    FrameQueue m_queue;           ///< 无锁队列：worker → main

    // 预定义 ID
    static constexpr quint32 STD_IDS[] = {0x100, 0x200, 0x300, 0x400, 0x500,
                                           0x600, 0x700, 0x7FF};
    static constexpr quint32 EXT_IDS[] = {0x18FEF100, 0x18FFA827, 0x18EAFFFE};

    /// 后台线程帧生成函数
    void workerLoop();
};

#endif // CANSIMULATOR_H
