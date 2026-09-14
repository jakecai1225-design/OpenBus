#ifndef CAPTURELOG_H
#define CAPTURELOG_H

#include <QObject>
#include <QVector>
#include <QMutex>
#include "core/canframe.h"
#include "utils/ringbuffer.h"

/**
 * @brief Process-wide capture ring — Phase B shared measurement history.
 *
 * Shell appends every batch once. Trace tabs pull via copyAfterSeq on their own
 * timer (decoupled from the ingress stack). Graphic may still receive a direct
 * fan-out until the shared sample store (B3) lands.
 *
 * Threading: appendBatch is called from the GUI drain path today; mutex allows a
 * future worker to append safely. Readers copy snapshots under the same lock.
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

    /**
     * @brief Copy frames appended after @p afterSeq (still resident in the ring).
     * @param afterSeq Exclusive lower bound (consumer's last applied seqCounter).
     * @param out Destination; cleared then filled with chronological frames.
     * @param newSeq If non-null, set to current seqCounter() for the consumer cursor.
     * @return Number of frames copied. If the ring wrapped past @p afterSeq, returns
     *         the entire resident ring (oldest loss is reported via newSeq jump).
     */
    int copyAfterSeq(quint64 afterSeq, QVector<CanFrame> *out, quint64 *newSeq = nullptr) const;

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
