#ifndef TRACEFILTERINDEX_H
#define TRACEFILTERINDEX_H

#include <QVector>
#include <QtGlobal>
#include <algorithm>

/**
 * @brief Accepted CaptureLog / Trace source-row index (T2).
 *
 * Stores only rows that pass the active filter, in capture (source) order.
 * Live append-only Trace maps through this list instead of a dense
 * source→proxy vector sized to the full history.
 *
 * Reverse lookup is O(log n) via binary search (rows stay sorted by source row).
 */
class TraceFilterIndex
{
public:
    void clear() { m_rows.clear(); }

    int size() const { return m_rows.size(); }
    bool isEmpty() const { return m_rows.isEmpty(); }

    int at(int index) const { return static_cast<int>(m_rows.at(index)); }

    const QVector<quint32> &rows() const { return m_rows; }
    QVector<quint32> &rowsMutable() { return m_rows; }

    void reserve(int n) { m_rows.reserve(n); }

    void append(int sourceRow)
    {
        m_rows.append(static_cast<quint32>(sourceRow));
    }

    void appendMany(const QVector<int> &sourceRows)
    {
        m_rows.reserve(m_rows.size() + sourceRows.size());
        for (int r : sourceRows)
            m_rows.append(static_cast<quint32>(r));
    }

    void setFromAccepted(const QVector<int> &accepted)
    {
        m_rows.clear();
        m_rows.reserve(accepted.size());
        for (int r : accepted)
            m_rows.append(static_cast<quint32>(r));
    }

    /// Proxy row for @p sourceRow, or -1 if filtered out.
    int indexOf(int sourceRow) const
    {
        if (sourceRow < 0 || m_rows.isEmpty())
            return -1;
        const auto begin = m_rows.cbegin();
        const auto end = m_rows.cend();
        const auto it = std::lower_bound(begin, end, static_cast<quint32>(sourceRow));
        if (it == end || *it != static_cast<quint32>(sourceRow))
            return -1;
        return static_cast<int>(it - begin);
    }

    /**
     * Ring wrap in capture order: keep old accepted rows with s >= shift as
     * (s - shift), then append newly accepted tail source rows.
     */
    void applyRingShift(int shift, const QVector<int> &newTailAccepted)
    {
        QVector<quint32> next;
        next.reserve(m_rows.size());
        for (quint32 s : m_rows) {
            if (static_cast<int>(s) >= shift)
                next.append(s - static_cast<quint32>(shift));
        }
        for (int r : newTailAccepted)
            next.append(static_cast<quint32>(r));
        m_rows = std::move(next);
    }

private:
    QVector<quint32> m_rows;
};

#endif // TRACEFILTERINDEX_H
