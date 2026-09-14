#include "capturelog.h"

#include <QtGlobal>

CaptureLog *CaptureLog::instance()
{
    static CaptureLog inst;
    return &inst;
}

CaptureLog::CaptureLog(QObject *parent)
    : QObject(parent)
    , m_ring(100000)
{
}

void CaptureLog::setCapacity(int capacity)
{
    QMutexLocker lock(&m_mutex);
    m_ring.reserve(capacity > 0 ? capacity : 1);
    m_seq = 0;
}

int CaptureLog::capacity() const
{
    QMutexLocker lock(&m_mutex);
    return m_ring.capacity();
}

int CaptureLog::size() const
{
    QMutexLocker lock(&m_mutex);
    return m_ring.size();
}

quint64 CaptureLog::seqCounter() const
{
    QMutexLocker lock(&m_mutex);
    return m_seq;
}

void CaptureLog::clear()
{
    QMutexLocker lock(&m_mutex);
    m_ring.clear();
    m_seq = 0;
    lock.unlock();
    emit cleared();
}

void CaptureLog::appendBatch(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;
    {
        QMutexLocker lock(&m_mutex);
        for (const auto &f : frames)
            m_ring.push(f);
        m_seq += static_cast<quint64>(frames.size());
    }
    emit batchAppended(frames.size());
}

int CaptureLog::copyRange(int from, int count, QVector<CanFrame> *out) const
{
    if (!out || count <= 0)
        return 0;
    QMutexLocker lock(&m_mutex);
    const int n = m_ring.size();
    if (from < 0 || from >= n)
        return 0;
    const int end = qMin(n, from + count);
    out->clear();
    out->reserve(end - from);
    for (int i = from; i < end; ++i)
        out->append(m_ring.at(i));
    return out->size();
}

bool CaptureLog::latest(CanFrame *out) const
{
    if (!out)
        return false;
    QMutexLocker lock(&m_mutex);
    if (m_ring.empty())
        return false;
    *out = m_ring.at(m_ring.size() - 1);
    return true;
}
