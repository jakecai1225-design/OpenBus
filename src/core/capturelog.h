#ifndef CAPTURELOG_H
#define CAPTURELOG_H

#include <QObject>
#include <QVector>
#include <QMutex>
#include "core/canframe.h"
#include "utils/ringbuffer.h"

/**
 * @brief Process-wide capture ring — Phase B foundation (Trace/Graphic Performance Plan).
 *
 * Single source of truth for measurement frames. Phase A still fans out to Trace/Graphic
 * models; Phase B consumers should read from here instead of keeping private 1e6 rings.
 *
 * Threading: appendBatch is called from the GUI drain path today; mutex allows a future
 * worker to append safely. Readers copy snapshots under the same lock.
 */
class CaptureLog : public QObject
{
    Q_OBJECT

public:
    static CaptureLog *instance();

    void setCapacity(int capacity);
    int capacity() const;
    int size() const;
    quint64 seqCounter() const;

    void clear();
    void appendBatch(const QVector<CanFrame> &frames);

    /// Copy logical rows [from, from+count) into out (0 = oldest). Returns copied count.
    int copyRange(int from, int count, QVector<CanFrame> *out) const;

    /// Latest frame if any.
    bool latest(CanFrame *out) const;

signals:
    void batchAppended(int count);
    void cleared();

private:
    explicit CaptureLog(QObject *parent = nullptr);

    mutable QMutex m_mutex;
    RingBuffer<CanFrame> m_ring;
    quint64 m_seq = 0;
};

#endif // CAPTURELOG_H
