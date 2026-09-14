#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <vector>

/**
 * @brief 泛型环形缓冲区
 *
 * 固定容量，push 满后覆盖最旧元素，O(1) 追加和随机访问。
 * 替代 QVector::removeFirst() 的 O(n) 内存移动。
 */
template <typename T>
class RingBuffer
{
public:
    explicit RingBuffer(int capacity = 1000000)
        : m_capacity(capacity > 0 ? capacity : 1), m_data(capacity > 0 ? capacity : 1) {}

    /// 追加元素。缓冲区满时覆盖最旧元素（返回被覆盖元素的指针，未满时返回 nullptr）
    const T *push(const T &item)
    {
        const T *overwritten = nullptr;
        if (m_count < m_capacity) {
            m_data[(m_head + m_count) % m_capacity] = item;
            m_count++;
        } else {
            overwritten = &m_data[m_head];
            m_data[m_head] = item;
            m_head = (m_head + 1) % m_capacity;
        }
        return overwritten;
    }

    /// 移动追加。缓冲区满时覆盖最旧元素。
    const T *push(T &&item)
    {
        const T *overwritten = nullptr;
        if (m_count < m_capacity) {
            m_data[(m_head + m_count) % m_capacity] = std::move(item);
            m_count++;
        } else {
            overwritten = &m_data[m_head];
            m_data[m_head] = std::move(item);
            m_head = (m_head + 1) % m_capacity;
        }
        return overwritten;
    }

    /// 按逻辑行号访问元素（0 = 最旧，size()-1 = 最新）
    const T &at(int index) const { return m_data[(m_head + index) % m_capacity]; }
    T &at(int index) { return m_data[(m_head + index) % m_capacity]; }

    int size() const { return m_count; }
    int capacity() const { return m_capacity; }
    bool full() const { return m_count >= m_capacity; }
    bool empty() const { return m_count == 0; }

    void clear()
    {
        m_head = 0;
        m_count = 0;
    }

    /// Grow capacity (logical order preserved). No-op if capacity <= current.
    void growTo(int capacity)
    {
        if (capacity <= m_capacity)
            return;
        std::vector<T> next(static_cast<size_t>(capacity));
        for (int i = 0; i < m_count; ++i)
            next[static_cast<size_t>(i)] = std::move((*this).at(i));
        m_data.swap(next);
        m_head = 0;
        m_capacity = capacity;
    }

    /// Reset to a new capacity and drop contents.
    void reserve(int capacity)
    {
        m_capacity = capacity > 0 ? capacity : 1;
        m_data.resize(static_cast<size_t>(m_capacity));
        m_head = 0;
        m_count = 0;
    }

private:
    std::vector<T> m_data;
    int m_head = 0;     ///< 指向最旧元素
    int m_count = 0;   ///< 当前元素数量
    int m_capacity;     ///< 固定容量
};

#endif // RINGBUFFER_H
