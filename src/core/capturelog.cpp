#include "capturelog.h"

#include <QtGlobal>
#include <cstring>

CaptureLog *CaptureLog::instance()
{
    static CaptureLog inst;
    return &inst;
}

CaptureLog::CaptureLog(QObject *parent)
    : QObject(parent)
{
    setCapacity(500000);
}

int CaptureLog::physicalIndex(int logical) const
{
    return (m_head + logical) % m_capacity;
}

void CaptureLog::fillFrame(int physical, CanFrame *out) const
{
    m_meta[static_cast<size_t>(physical)].applyMetaTo(out);
    const quint8 len = m_meta[static_cast<size_t>(physical)].dataLen;
    if (len == 0) {
        out->data.clear();
        return;
    }
    const quint8 *src = m_payload.data() + physical * kPayloadBytes;
    out->data = QByteArray(reinterpret_cast<const char *>(src), len);
}

void CaptureLog::pushOne(const CanFrame &f)
{
    int phys = 0;
    if (m_count < m_capacity) {
        phys = (m_head + m_count) % m_capacity;
        ++m_count;
    } else {
        phys = m_head;
        m_head = (m_head + 1) % m_capacity;
    }

    CaptureFrameMeta meta = CaptureFrameMeta::fromFrame(f);
    m_meta[static_cast<size_t>(phys)] = meta;
    if (meta.dataLen > 0) {
        std::memcpy(m_payload.data() + phys * kPayloadBytes,
                    f.data.constData(),
                    meta.dataLen);
    }
}

void CaptureLog::setCapacity(int capacity)
{
    QMutexLocker lock(&m_mutex);
    m_capacity = capacity > 0 ? capacity : 1;
    m_head = 0;
    m_count = 0;
    m_seq = 0;
    m_meta.assign(static_cast<size_t>(m_capacity), CaptureFrameMeta{});
    m_payload.assign(static_cast<size_t>(m_capacity) * kPayloadBytes, 0);
}

int CaptureLog::capacity() const
{
    QMutexLocker lock(&m_mutex);
    return m_capacity;
}

int CaptureLog::size() const
{
    QMutexLocker lock(&m_mutex);
    return m_count;
}

quint64 CaptureLog::seqCounter() const
{
    QMutexLocker lock(&m_mutex);
    return m_seq;
}

void CaptureLog::snapshot(int *size, quint64 *seq, int *capacity) const
{
    QMutexLocker lock(&m_mutex);
    if (size)
        *size = m_count;
    if (seq)
        *seq = m_seq;
    if (capacity)
        *capacity = m_capacity;
}

void CaptureLog::clear()
{
    QMutexLocker lock(&m_mutex);
    m_head = 0;
    m_count = 0;
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
            pushOne(f);
        m_seq += static_cast<quint64>(frames.size());
    }
    emit batchAppended(frames.size());
}

bool CaptureLog::frameMetaAt(int row, CaptureFrameMeta *out) const
{
    if (!out)
        return false;
    QMutexLocker lock(&m_mutex);
    if (row < 0 || row >= m_count)
        return false;
    *out = m_meta[static_cast<size_t>(physicalIndex(row))];
    return true;
}

bool CaptureLog::copyDataAt(int row, QByteArray *out) const
{
    if (!out)
        return false;
    QMutexLocker lock(&m_mutex);
    if (row < 0 || row >= m_count)
        return false;
    const int phys = physicalIndex(row);
    const quint8 len = m_meta[static_cast<size_t>(phys)].dataLen;
    if (len == 0) {
        out->clear();
        return true;
    }
    const quint8 *src = m_payload.data() + phys * kPayloadBytes;
    *out = QByteArray(reinterpret_cast<const char *>(src), len);
    return true;
}

bool CaptureLog::frameAt(int row, CanFrame *out) const
{
    if (!out)
        return false;
    QMutexLocker lock(&m_mutex);
    if (row < 0 || row >= m_count)
        return false;
    fillFrame(physicalIndex(row), out);
    return true;
}

int CaptureLog::copyRange(int from, int count, QVector<CanFrame> *out) const
{
    if (!out || count <= 0)
        return 0;
    QMutexLocker lock(&m_mutex);
    if (from < 0 || from >= m_count)
        return 0;
    const int end = qMin(m_count, from + count);
    out->clear();
    out->reserve(end - from);
    for (int i = from; i < end; ++i) {
        CanFrame f;
        fillFrame(physicalIndex(i), &f);
        out->append(std::move(f));
    }
    return out->size();
}

static void unreadWindow(quint64 afterSeq, quint64 seq, int n, int maxCount,
                         int *from, int *take, quint64 *newSeq)
{
    *from = 0;
    *take = 0;
    if (afterSeq >= seq || n <= 0) {
        if (newSeq)
            *newSeq = seq;
        return;
    }
    const quint64 want = seq - afterSeq;
    const int unread = static_cast<int>(qMin<quint64>(want, static_cast<quint64>(n)));
    *from = n - unread;
    *take = unread;
    if (maxCount > 0 && *take > maxCount)
        *take = maxCount;
    if (newSeq) {
        if (want > static_cast<quint64>(n))
            *newSeq = seq - static_cast<quint64>(n) + static_cast<quint64>(*take);
        else
            *newSeq = afterSeq + static_cast<quint64>(*take);
    }
}

int CaptureLog::copyAfterSeq(quint64 afterSeq, QVector<CanFrame> *out, quint64 *newSeq,
                             int maxCount) const
{
    if (!out)
        return 0;
    QMutexLocker lock(&m_mutex);
    out->clear();
    int from = 0, take = 0;
    unreadWindow(afterSeq, m_seq, m_count, maxCount, &from, &take, newSeq);
    if (take <= 0)
        return 0;
    out->reserve(take);
    for (int i = 0; i < take; ++i) {
        CanFrame f;
        fillFrame(physicalIndex(from + i), &f);
        out->append(std::move(f));
    }
    return take;
}

int CaptureLog::visitAfterSeq(quint64 afterSeq,
                              const std::function<void(const CanFrame &)> &fn,
                              quint64 *newSeq, int maxCount) const
{
    if (!fn)
        return 0;
    QMutexLocker lock(&m_mutex);
    int from = 0, take = 0;
    unreadWindow(afterSeq, m_seq, m_count, maxCount, &from, &take, newSeq);
    for (int i = 0; i < take; ++i) {
        CanFrame f;
        fillFrame(physicalIndex(from + i), &f);
        fn(f);
    }
    return take;
}

bool CaptureLog::latest(CanFrame *out) const
{
    if (!out)
        return false;
    QMutexLocker lock(&m_mutex);
    if (m_count <= 0)
        return false;
    fillFrame(physicalIndex(m_count - 1), out);
    return true;
}
