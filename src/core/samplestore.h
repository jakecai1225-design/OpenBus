#ifndef SAMPLESTORE_H
#define SAMPLESTORE_H

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QPair>
#include <QThread>
#include <QVector>
#include <atomic>
#include <cmath>

#include "core/canframe.h"
#include "core/dbcdata.h"
#include "utils/ringbuffer.h"

/**
 * @brief Process-wide decoded sample rings — Phase B3/B4 + P0-1 LOD.
 *
 * Keyed by (canId, extended, startBit, bitLength, endian, signed, factor, offset).
 * Only subscribed series are updated on ingest (unsubscribed IDs cost ~0).
 * Multiple Graphic tabs sharing the same signal share one ring via refcount.
 *
 * Phase B4: ingestFrames queues batches to a worker thread; decode + ring push
 * + incremental MinMax LOD buckets run off the GUI.
 * Graphic views pull raw via copyAfterSeq (cursors) and display via copyDownsampled.
 */
struct SampleKey {
    quint32 canId = 0;
    bool extended = false;
    int startBit = 0;
    int bitLength = 0;
    bool littleEndian = true;
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;

    static SampleKey fromSignal(quint32 canId, bool extended, const DbcSignal &sig);

    bool operator==(const SampleKey &o) const;
};

size_t qHash(const SampleKey &k, size_t seed = 0) noexcept;

struct SignalSample {
    double t = 0.0;
    double v = 0.0;
};

Q_DECLARE_METATYPE(SampleKey)

class SampleStore : public QObject
{
    Q_OBJECT

public:
    static SampleStore *instance();

    void setCapacity(int capacity);
    int capacity() const;

    void clear();

    /// Increment refcount; creates series if needed. Returns false on invalid layout.
    bool subscribe(const SampleKey &key, const DbcSignal &decode);

    /// Decrement refcount; removes series at zero.
    void unsubscribe(const SampleKey &key);

    bool isSubscribed(const SampleKey &key) const;

    /// Queue frames for off-GUI decode (Phase B4). Cheap on the caller thread.
    void ingestFrames(const QVector<CanFrame> &frames);

    /// Append already-decoded samples (history backfill; runs synchronously).
    void appendSamples(const SampleKey &key, const QVector<SignalSample> &samples);

    /**
     * Copy newest samples after @p afterSeq (still resident).
     * @param maxCount If > 0, copy at most this many (newest). Cursor still jumps to tip.
     */
    int copyAfterSeq(const SampleKey &key, quint64 afterSeq,
                     QVector<SignalSample> *out, quint64 *newSeq = nullptr,
                     int maxCount = -1) const;

    /**
     * P0-1: Copy viewport display points (~targetPoints).
     * When the viewport holds ≤ targetPoints raw samples, copies those samples
     * (CANoe-style zoom-in: true markers + jumps). Otherwise MinMax from LOD.
     */
    int copyDownsampled(const SampleKey &key, double t1, double t2, int targetPoints,
                        QVector<SignalSample> *out) const;

    /// Running Y range maintained on ingest (no full raw scan).
    bool valueRange(const SampleKey &key, double *minOut, double *maxOut) const;

    /// Oldest/newest sample times in the ring (fit X / overview).
    bool timeRange(const SampleKey &key, double *tMinOut, double *tMaxOut) const;

    /// Latest sample tip (for time cursor without full pull).
    bool latestSample(const SampleKey &key, SignalSample *out, quint64 *seqOut = nullptr) const;

    /**
     * Decode frames on the calling thread with no ingest-queue soft-drop.
     * Used for offline file analysis bulk load (preserve continuous file timestamps).
     * Live bus path should keep using ingestFrames().
     */
    void ingestFramesDirect(const QVector<CanFrame> &frames);

    /// Interpolate value at time (cursors); short lock, O(log n).
    bool valueAtTime(const SampleKey &key, double t, double *outVal) const;

    quint64 seqCounter(const SampleKey &key) const;
    int sampleCount(const SampleKey &key) const;

signals:
    void seriesUpdated(const SampleKey &key, int appended);
    void cleared();
    /// Internal: wake the decode worker (queued to worker thread).
    void ingestWake();

private slots:
    void processIngestQueue();

private:
    /// One MinMax bucket over samplesPerBucket consecutive samples (P0-1).
    struct LodBucket {
        double tFirst = 0.0;
        double tLast = 0.0;
        double vMin = 0.0;
        double vMax = 0.0;
        double tAtMin = 0.0;
        double tAtMax = 0.0;
    };

    struct Series {
        SampleKey key;
        DbcSignal decode;
        RingBuffer<SignalSample> ring{65536};
        quint64 seq = 0;
        int refCount = 0;

        // ---- P0-1 LOD (updated in pushOne on worker / sync append) ----
        static constexpr int kLodCapacity = 4096;
        RingBuffer<LodBucket> lod{kLodCapacity};
        LodBucket open;
        int openCount = 0;
        int samplesPerBucket = 48;
        double dataMin = 0.0;
        double dataMax = 0.0;
        bool hasRange = false;
    };

    explicit SampleStore(QObject *parent = nullptr);
    ~SampleStore() override;

    Series *findSeries(const SampleKey &key);
    const Series *findSeries(const SampleKey &key) const;
    void rebuildIdIndex();
    void pushOne(Series *s, double t, double v);
    void resetLod(Series *s);
    void ingestFramesLocked(const QVector<CanFrame> &frames,
                            QVector<QPair<SampleKey, int>> *touched);
    static int downsampleRaw(const Series *s, double t1, double t2, int targetPoints,
                             QVector<SignalSample> *out);
    static int downsampleFromBuckets(const QVector<LodBucket> &buckets,
                                     double t1, double t2, int targetPoints,
                                     QVector<SignalSample> *out);

    mutable QMutex m_mutex;
    QHash<SampleKey, Series *> m_series;
    /// canId<<1|ext → series list (subscribed only)
    QHash<quint64, QVector<Series *>> m_idIndex;
    int m_capacity = 200000;

    // ---- B4 worker ----
    QThread m_workerThread;
    QObject *m_worker = nullptr;   ///< lives on m_workerThread
    QMutex m_queueMutex;
    QVector<QVector<CanFrame>> m_ingestQueue;
    int m_queuedFrames = 0;            ///< soft-cap total frames waiting
    std::atomic<bool> m_stopping{false};
};

#endif // SAMPLESTORE_H
