#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

/**
 * @brief 基于 moodycamel::ConcurrentQueue 的 CAN 帧无锁消息队列
 *
 * 提供生产者-消费者模型的核心数据通道：
 *   - 硬件 CAN 接收线程 → enqueue → 主线程 drainAndDispatch
 *   - 回放器线程 → enqueue → 主线程 drainAndDispatch
 *   - 模拟器后台线程 → enqueue → 主线程 drainAndDispatch
 *
 * 优势（相比 QQueue + QMutex）：
 *   - 无锁设计，多线程无竞争，性能高 3-5 倍
 *   - 批量 enqueue/dequeue 接口，减少线程交互次数
 *   - 自动扩容，无固定容量限制
 */

#include "core/canframe.h"

#include <concurrentqueue.h>
#include <QVector>

/**
 * @brief CAN 帧无锁队列
 *
 * 封装 moodycamel::ConcurrentQueue<CanFrame>，提供 Qt 友好的接口。
 * 线程安全：任意线程可 enqueue，任意线程可 dequeue。
 */
class FrameQueue
{
public:
    FrameQueue() = default;

    // ---- 禁止拷贝 ----
    FrameQueue(const FrameQueue &) = delete;
    FrameQueue &operator=(const FrameQueue &) = delete;

    /// 入队单帧（线程安全，无阻塞）
    void enqueue(const CanFrame &frame)
    {
        m_queue.enqueue(frame);
    }

    /// 入队单帧（移动语义）
    void enqueue(CanFrame &&frame)
    {
        m_queue.enqueue(std::move(frame));
    }

    /// 批量入队（减少线程交互开销）
    void enqueueBatch(const QVector<CanFrame> &frames)
    {
        if (frames.isEmpty())
            return;
        m_queue.enqueue_bulk(frames.data(), static_cast<size_t>(frames.size()));
    }

    /// 尝试出队单帧（非阻塞）
    /// @return true 如果成功取出，false 如果队列为空
    bool tryDequeue(CanFrame &out)
    {
        return m_queue.try_dequeue(out);
    }

    /// 批量出队（非阻塞，最多取 maxCount 帧）
    /// @return 实际取出的帧数
    size_t tryDequeueBulk(QVector<CanFrame> &out, size_t maxCount)
    {
        out.resize(static_cast<int>(maxCount));
        size_t got = m_queue.try_dequeue_bulk(
            out.data(), static_cast<size_t>(maxCount));
        out.resize(static_cast<int>(got));
        return got;
    }

    /// 队列中大约的帧数（非精确，用于监控）
    size_t approxSize() const
    {
        return m_queue.size_approx();
    }

private:
    moodycamel::ConcurrentQueue<CanFrame> m_queue;
};

#endif // MESSAGE_QUEUE_H
