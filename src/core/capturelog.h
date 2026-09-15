#ifndef CAPTURELOG_H
#define CAPTURELOG_H

#include <QObject>
#include <QVector>
#include <QMutex>
#include <QByteArray>
#include <QtGlobal>
#include <functional>
#include <vector>
#include "core/canframe.h"

/**
 * @brief Hot fields for one CaptureLog row (no QByteArray).
 *
 * Contiguous SoA meta ring + fixed 64-byte payload slabs. Trace paint should
 * use frameMetaAt / copyDataAt instead of deep-copying CanFrame per cell.
 */
struct CaptureFrameMeta
{
    double timestamp = 0.0;
    quint64 timestampNs = 0;
    quint32 id = 0;
    quint8 dlc = 0;
    quint8 channel = 1;
    quint8 flags = 0;   ///< Same bit layout as CanFrame stream flags
    quint8 dataLen = 0; ///< Bytes stored in the payload slab (0..64)

    enum FlagBits : quint8 {
        Ext = 0x01,
        Fd = 0x02,
        Brs = 0x04,
        Esi = 0x08,
        Tx = 0x10
    };

    bool extended() const { return (flags & Ext) != 0; }
    bool fd() const { return (flags & Fd) != 0; }
    bool bitrateSwitch() const { return (flags & Brs) != 0; }
    bool errorState() const { return (flags & Esi) != 0; }
    bool isErrorFrame() const { return (id & 0x20000000) != 0; }
    CanFrame::Direction direction() const
    {
        return (flags & Tx) ? CanFrame::Tx : CanFrame::Rx;
    }

    static quint8 packFlags(const CanFrame &f)
    {
        return static_cast<quint8>(
            (f.extended ? Ext : 0) |
            (f.fd ? Fd : 0) |
            (f.bitrateSwitch ? Brs : 0) |
            (f.errorState ? Esi : 0) |
            (f.direction == CanFrame::Tx ? Tx : 0));
    }

    static CaptureFrameMeta fromFrame(const CanFrame &f)
    {
        CaptureFrameMeta m;
        m.timestamp = f.timestamp;
        m.timestampNs = f.timestampNs;
        m.id = f.id;
        m.dlc = f.dlc;
        m.channel = f.channel;
        m.flags = packFlags(f);
        const int n = qMin(64, f.data.size());
        m.dataLen = static_cast<quint8>(n);
        return m;
    }

    /// Fill CanFrame fields except payload (data left unchanged / empty).
    void applyMetaTo(CanFrame *out) const
    {
        if (!out)
            return;
        out->timestamp = timestamp;
        out->timestampNs = timestampNs;
        out->id = id;
        out->dlc = dlc;
        out->channel = channel;
        out->extended = extended();
        out->fd = fd();
        out->bitrateSwitch = bitrateSwitch();
        out->errorState = errorState();
        out->direction = direction();
    }

    CanFrame toFrameSkeleton() const
    {
        CanFrame f;
        applyMetaTo(&f);
        return f;
    }
};

/**
 * @brief Process-wide capture ring — Phase B shared measurement history.
 *
 * Shell appends every batch once. Trace tabs are cameras: they sync rowCount/seq
 * and read via frameMetaAt / frameAt (no per-tab ring copy on the live path).
 *
 * Storage is SoA: contiguous CaptureFrameMeta + side payload slabs (64 B/row).
 *
 * Threading: appendBatch is called from the GUI drain path today; mutex allows a
 * future worker to append safely. Readers copy under the same lock.
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

    /// size / seq / capacity under one lock (Trace camera sync).
    void snapshot(int *size, quint64 *seq, int *capacity = nullptr) const;

    void clear();
    void appendBatch(const QVector<CanFrame> &frames);

    /// Cheap meta copy (no payload). Returns false if out of range.
    bool frameMetaAt(int row, CaptureFrameMeta *out) const;

    /// Copy payload bytes only into @p out. Returns false if out of range.
    bool copyDataAt(int row, QByteArray *out) const;

    /// Full CanFrame copy (meta + payload). Prefer frameMetaAt on paint path.
    bool frameAt(int row, CanFrame *out) const;

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
    int copyAfterSeq(quint64 afterSeq, QVector<CanFrame> *out, quint64 *newSeq = nullptr,
                     int maxCount = -1) const;

    /// Visit frames after @p afterSeq without allocating a batch vector.
    /// @p maxCount > 0 limits how many frames are visited; @p newSeq then advances
    /// by that count (streaming camera) instead of jumping to the log tip.
    int visitAfterSeq(quint64 afterSeq, const std::function<void(const CanFrame &)> &fn,
                      quint64 *newSeq = nullptr, int maxCount = -1) const;

    /// Latest frame if any.
    bool latest(CanFrame *out) const;

signals:
    void batchAppended(int count);
    void cleared();

private:
    explicit CaptureLog(QObject *parent = nullptr);

    int physicalIndex(int logical) const;
    void pushOne(const CanFrame &f);
    void fillFrame(int physical, CanFrame *out) const;

    mutable QMutex m_mutex;
    int m_capacity = 1;
    int m_head = 0;
    int m_count = 0;
    std::vector<CaptureFrameMeta> m_meta;
    std::vector<quint8> m_payload; ///< m_capacity * 64
    quint64 m_seq = 0;

    static constexpr int kPayloadBytes = 64;
};

#endif // CAPTURELOG_H
