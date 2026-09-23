#include "samplestore.h"

#include <QPair>
#include <QtGlobal>
#include <algorithm>

SampleKey SampleKey::fromSignal(quint32 canId, bool extended, const DbcSignal &sig)
{
    SampleKey k;
    k.canId = canId & 0x1FFFFFFF;
    k.extended = extended;
    k.startBit = sig.startBit;
    k.bitLength = sig.bitLength;
    k.littleEndian = sig.littleEndian;
    k.isSigned = sig.isSigned;
    k.factor = sig.factor;
    k.offset = sig.offset;
    return k;
}

bool SampleKey::operator==(const SampleKey &o) const
{
    return canId == o.canId
        && extended == o.extended
        && startBit == o.startBit
        && bitLength == o.bitLength
        && littleEndian == o.littleEndian
        && isSigned == o.isSigned
        && qFuzzyCompare(factor + 1.0, o.factor + 1.0)
        && qFuzzyCompare(offset + 1.0, o.offset + 1.0);
}

size_t qHash(const SampleKey &k, size_t seed) noexcept
{
    seed = ::qHash(k.canId, seed);
    seed = ::qHash(static_cast<int>(k.extended), seed);
    seed = ::qHash(k.startBit, seed);
    seed = ::qHash(k.bitLength, seed);
    seed = ::qHash(static_cast<int>(k.littleEndian), seed);
    seed = ::qHash(static_cast<int>(k.isSigned), seed);
    seed = ::qHash(k.factor, seed);
    seed = ::qHash(k.offset, seed);
    return seed;
}

SampleStore *SampleStore::instance()
{
    static SampleStore inst;
    return &inst;
}

SampleStore::SampleStore(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SampleKey>("SampleKey");

    m_worker = new QObject;
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &SampleStore::ingestWake, m_worker, [this]() {
        processIngestQueue();
    }, Qt::QueuedConnection);
    m_workerThread.start(QThread::LowPriority);
}

SampleStore::~SampleStore()
{
    m_stopping.store(true);
    {
        QMutexLocker lock(&m_queueMutex);
        m_ingestQueue.clear();
    }
    m_workerThread.quit();
    m_workerThread.wait(3000);

    QMutexLocker lock(&m_mutex);
    qDeleteAll(m_series);
    m_series.clear();
    m_idIndex.clear();
}

void SampleStore::resetLod(Series *s)
{
    if (!s)
        return;
    s->lod.clear();
    s->lod.reserve(Series::kLodCapacity);
    s->open = LodBucket{};
    s->openCount = 0;
    s->samplesPerBucket = qMax(1, m_capacity / Series::kLodCapacity);
    s->hasRange = false;
    s->dataMin = 0.0;
    s->dataMax = 0.0;
}

void SampleStore::setCapacity(int capacity)
{
    QMutexLocker lock(&m_mutex);
    m_capacity = capacity > 0 ? capacity : 1;
    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        Series *s = it.value();
        s->ring.reserve(m_capacity);
        s->seq = 0;
        resetLod(s);
    }
}

int SampleStore::capacity() const
{
    QMutexLocker lock(&m_mutex);
    return m_capacity;
}

void SampleStore::clear()
{
    {
        QMutexLocker qlock(&m_queueMutex);
        m_ingestQueue.clear();
        m_queuedFrames = 0;
    }
    QMutexLocker lock(&m_mutex);
    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        Series *s = it.value();
        s->ring.clear();
        s->seq = 0;
        resetLod(s);
    }
    lock.unlock();
    emit cleared();
}

bool SampleStore::subscribe(const SampleKey &key, const DbcSignal &decode)
{
    if (key.bitLength <= 0)
        return false;
    QMutexLocker lock(&m_mutex);
    Series *s = findSeries(key);
    if (s) {
        s->refCount++;
        return true;
    }
    s = new Series;
    s->key = key;
    s->decode = decode;
    s->ring.reserve(m_capacity);
    s->refCount = 1;
    resetLod(s);
    m_series.insert(key, s);
    rebuildIdIndex();
    return true;
}

void SampleStore::unsubscribe(const SampleKey &key)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_series.find(key);
    if (it == m_series.end())
        return;
    Series *s = it.value();
    s->refCount--;
    if (s->refCount > 0)
        return;
    m_series.erase(it);
    delete s;
    rebuildIdIndex();
}

bool SampleStore::isSubscribed(const SampleKey &key) const
{
    QMutexLocker lock(&m_mutex);
    return m_series.contains(key);
}

void SampleStore::ingestFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty() || m_stopping.load())
        return;
    {
        QMutexLocker lock(&m_queueMutex);
        // Soft-cap: avoid unbounded QVector growth (crash after long runs).
        // Live bus only — offline bulk must use ingestFramesDirect() (no drop).
        constexpr int kMaxQueuedBatches = 32;
        constexpr int kMaxQueuedFrames = 65536;
        if (m_queuedFrames >= kMaxQueuedFrames && !m_ingestQueue.isEmpty()) {
            m_queuedFrames -= m_ingestQueue.first().size();
            if (m_queuedFrames < 0)
                m_queuedFrames = 0;
            m_ingestQueue.removeFirst();
        }
        if (m_ingestQueue.size() >= kMaxQueuedBatches && !m_ingestQueue.isEmpty()) {
            m_ingestQueue.last() += frames;
        } else {
            m_ingestQueue.append(frames);
        }
        m_queuedFrames += frames.size();
    }
    emit ingestWake();
}

void SampleStore::ingestFramesDirect(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty() || m_stopping.load())
        return;
    QVector<QPair<SampleKey, int>> touched;
    ingestFramesLocked(frames, &touched);
    for (const auto &t : touched)
        emit seriesUpdated(t.first, t.second);
}

void SampleStore::processIngestQueue()
{
    if (m_stopping.load())
        return;

    QVector<QVector<CanFrame>> batches;
    {
        QMutexLocker lock(&m_queueMutex);
        batches.swap(m_ingestQueue);
        m_queuedFrames = 0;
    }
    if (batches.isEmpty())
        return;

    QVector<QPair<SampleKey, int>> touched;
    for (const auto &batch : batches)
        ingestFramesLocked(batch, &touched);

    for (const auto &t : touched)
        emit seriesUpdated(t.first, t.second);
}

void SampleStore::ingestFramesLocked(const QVector<CanFrame> &frames,
                                     QVector<QPair<SampleKey, int>> *touched)
{
    if (frames.isEmpty() || !touched)
        return;

    QMutexLocker lock(&m_mutex);
    if (m_series.isEmpty())
        return;

    for (const auto &frame : frames) {
        const quint64 idKey =
            (quint64(frame.id & 0x1FFFFFFF) << 1) | (frame.extended ? 1ull : 0ull);
        const auto seriesList = m_idIndex.value(idKey);
        for (Series *s : seriesList) {
            const double val = s->decode.decode(frame.data);
            if (std::isnan(val))
                continue;
            pushOne(s, frame.timestamp, val);
            bool found = false;
            for (auto &t : *touched) {
                if (t.first == s->key) {
                    t.second++;
                    found = true;
                    break;
                }
            }
            if (!found)
                touched->append({s->key, 1});
        }
    }
}

void SampleStore::appendSamples(const SampleKey &key, const QVector<SignalSample> &samples)
{
    if (samples.isEmpty())
        return;
    int n = 0;
    {
        QMutexLocker lock(&m_mutex);
        Series *s = findSeries(key);
        if (!s)
            return;
        for (const auto &sm : samples)
            pushOne(s, sm.t, sm.v);
        n = samples.size();
    }
    emit seriesUpdated(key, n);
}

int SampleStore::copyAfterSeq(const SampleKey &key, quint64 afterSeq,
                              QVector<SignalSample> *out, quint64 *newSeq,
                              int maxCount) const
{
    if (!out)
        return 0;
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    if (!s) {
        out->clear();
        if (newSeq)
            *newSeq = afterSeq;
        return 0;
    }
    if (newSeq)
        *newSeq = s->seq;
    out->clear();
    if (afterSeq >= s->seq || s->ring.empty())
        return 0;

    const int n = s->ring.size();
    const quint64 want = s->seq - afterSeq;
    int take = static_cast<int>(qMin<quint64>(want, static_cast<quint64>(n)));
    // Cap: keep newest only (cursor buffer / avoid GUI OOM)
    if (maxCount > 0 && take > maxCount)
        take = maxCount;
    const int from = n - take;
    out->reserve(take);
    for (int i = from; i < n; ++i)
        out->append(s->ring.at(i));
    return take;
}

bool SampleStore::valueRange(const SampleKey &key, double *minOut, double *maxOut) const
{
    if (!minOut || !maxOut)
        return false;
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    if (!s || !s->hasRange)
        return false;
    *minOut = s->dataMin;
    *maxOut = s->dataMax;
    return true;
}

bool SampleStore::timeRange(const SampleKey &key, double *tMinOut, double *tMaxOut) const
{
    if (!tMinOut || !tMaxOut)
        return false;
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    if (!s || s->ring.empty())
        return false;
    *tMinOut = s->ring.at(0).t;
    *tMaxOut = s->ring.at(s->ring.size() - 1).t;
    return true;
}

bool SampleStore::latestSample(const SampleKey &key, SignalSample *out, quint64 *seqOut) const
{
    if (!out)
        return false;
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    if (!s || s->ring.empty())
        return false;
    *out = s->ring.at(s->ring.size() - 1);
    if (seqOut)
        *seqOut = s->seq;
    return true;
}

bool SampleStore::valueAtTime(const SampleKey &key, double t, double *outVal) const
{
    if (!outVal)
        return false;
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    if (!s || s->ring.empty())
        return false;
    const int n = s->ring.size();
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (s->ring.at(mid).t < t)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == n) {
        *outVal = s->ring.at(n - 1).v;
        return true;
    }
    if (lo == 0) {
        *outVal = s->ring.at(0).v;
        return true;
    }
    const SignalSample &p0 = s->ring.at(lo - 1);
    const SignalSample &p1 = s->ring.at(lo);
    if (p1.t == p0.t) {
        *outVal = p1.v;
    } else {
        const double a = (t - p0.t) / (p1.t - p0.t);
        *outVal = p0.v + a * (p1.v - p0.v);
    }
    return true;
}

static void appendBucketPoints(QVector<SignalSample> *out,
                               double tAtMin, double vMin,
                               double tAtMax, double vMax)
{
    if (tAtMin <= tAtMax) {
        out->append({tAtMin, vMin});
        if (tAtMax > tAtMin)
            out->append({tAtMax, vMax});
    } else {
        out->append({tAtMax, vMax});
        out->append({tAtMin, vMin});
    }
}

int SampleStore::downsampleFromBuckets(const QVector<LodBucket> &buckets,
                                       double t1, double t2, int targetPoints,
                                       QVector<SignalSample> *out)
{
    out->clear();
    if (buckets.isEmpty() || t2 <= t1 || targetPoints < 2)
        return 0;

    const int bucketTarget = std::max(1, targetPoints / 2);
    if (buckets.size() <= bucketTarget) {
        out->reserve(buckets.size() * 2 + 2);
        for (const auto &b : buckets)
            appendBucketPoints(out, b.tAtMin, b.vMin, b.tAtMax, b.vMax);
        return out->size();
    }

    // Bucket each display time-group independently. A single LOD bucket may span
    // many groups — do NOT consume it on the first hit (that left middle gaps).
    out->reserve(bucketTarget * 2 + 2);
    const double span = (t2 - t1) / bucketTarget;
    int i = 0;
    for (int g = 0; g < bucketTarget; ++g) {
        const double gStart = t1 + g * span;
        const double gEnd = gStart + span;
        while (i < buckets.size() && buckets.at(i).tLast < gStart)
            ++i;
        LodBucket merged;
        bool has = false;
        for (int j = i; j < buckets.size() && buckets.at(j).tFirst < gEnd; ++j) {
            const LodBucket &b = buckets.at(j);
            if (b.tLast < gStart)
                continue;
            if (!has) {
                merged = b;
                has = true;
            } else {
                if (b.tFirst < merged.tFirst)
                    merged.tFirst = b.tFirst;
                if (b.tLast > merged.tLast)
                    merged.tLast = b.tLast;
                if (b.vMin < merged.vMin) {
                    merged.vMin = b.vMin;
                    merged.tAtMin = b.tAtMin;
                }
                if (b.vMax > merged.vMax) {
                    merged.vMax = b.vMax;
                    merged.tAtMax = b.tAtMax;
                }
            }
        }
        // Advance past buckets that end before this group ends; keep those that
        // still overlap later groups.
        while (i < buckets.size() && buckets.at(i).tLast < gEnd)
            ++i;
        if (has)
            appendBucketPoints(out, merged.tAtMin, merged.vMin, merged.tAtMax, merged.vMax);
    }
    return out->size();
}

int SampleStore::downsampleRaw(const Series *s, double t1, double t2, int targetPoints,
                               QVector<SignalSample> *out)
{
    out->clear();
    const int n = s->ring.size();
    if (n == 0 || t2 <= t1)
        return 0;

    auto lowerBound = [&](double key) {
        int lo = 0, hi = n;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (s->ring.at(mid).t < key)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    };

    int first = lowerBound(t1);
    int last = lowerBound(t2);
    if (first > 0)
        first--;
    if (last < n - 1)
        last++;
    const int inRange = last - first;
    if (inRange <= 0)
        return 0;

    if (inRange <= targetPoints || targetPoints < 2) {
        out->reserve(inRange);
        for (int i = first; i < last; ++i)
            out->append(s->ring.at(i));
        return out->size();
    }

    const int bucketCount = std::max(1, targetPoints / 2);
    const double bucketSpan = (t2 - t1) / bucketCount;
    out->reserve(bucketCount * 2 + 2);
    int i = first;
    for (int b = 0; b < bucketCount && i < last; ++b) {
        const double bStart = t1 + b * bucketSpan;
        const double bEnd = bStart + bucketSpan;
        double minV = 0, maxV = 0, minT = 0, maxT = 0;
        bool has = false;
        while (i < last && s->ring.at(i).t < bStart)
            ++i;
        while (i < last && s->ring.at(i).t < bEnd) {
            const SignalSample &sm = s->ring.at(i);
            if (!has) {
                minV = maxV = sm.v;
                minT = maxT = sm.t;
                has = true;
            } else {
                if (sm.v < minV) { minV = sm.v; minT = sm.t; }
                if (sm.v > maxV) { maxV = sm.v; maxT = sm.t; }
            }
            ++i;
        }
        if (!has)
            continue;
        if (minT <= maxT) {
            out->append({minT, minV});
            if (maxT > minT)
                out->append({maxT, maxV});
        } else {
            out->append({maxT, maxV});
            out->append({minT, minV});
        }
    }

    if (!out->isEmpty()) {
        const SignalSample &head = s->ring.at(first);
        const SignalSample &tail = s->ring.at(last - 1);
        if (out->first().t != head.t)
            out->prepend(head);
        if (out->last().t != tail.t)
            out->append(tail);
    }
    return out->size();
}

int SampleStore::copyDownsampled(const SampleKey &key, double t1, double t2, int targetPoints,
                                 QVector<SignalSample> *out) const
{
    if (!out)
        return 0;

    // Snapshot under lock. Prefer raw when viewport density is low enough that
    // every sample fits the display budget (zoom-in → true points; zoom-out → LOD).
    QVector<LodBucket> window;
    {
        QMutexLocker lock(&m_mutex);
        const Series *s = findSeries(key);
        if (!s || s->ring.empty() || t2 <= t1) {
            out->clear();
            return 0;
        }

        // Binary-search raw span in viewport (+1 bracketing sample each side).
        const int n = s->ring.size();
        auto lowerBound = [&](double keyT) {
            int lo = 0, hi = n;
            while (lo < hi) {
                const int mid = lo + (hi - lo) / 2;
                if (s->ring.at(mid).t < keyT)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            return lo;
        };
        int first = lowerBound(t1);
        int last = lowerBound(t2);
        if (first > 0)
            --first;
        if (last < n)
            ++last;
        const int inRange = last - first;
        if (inRange > 0 && (inRange <= targetPoints || targetPoints < 2)) {
            out->clear();
            out->reserve(inRange);
            for (int i = first; i < last; ++i)
                out->append(s->ring.at(i));
            return out->size();
        }

        // Prefer raw MinMax for the viewport: LOD display buckets previously
        // skipped mid-span groups (visual holes while data was continuous).
        // Raw path is O(inRange) with binary-searched bounds — fine for replot.
        constexpr int kRawDownsampleMax = 400000;
        if (inRange > 0 && inRange <= kRawDownsampleMax)
            return downsampleRaw(s, t1, t2, targetPoints, out);

        window.reserve(s->lod.size() + 1);
        for (int i = 0; i < s->lod.size(); ++i) {
            const LodBucket &b = s->lod.at(i);
            if (b.tLast < t1 || b.tFirst > t2)
                continue;
            window.append(b);
        }
        if (s->openCount > 0) {
            const LodBucket &b = s->open;
            if (!(b.tLast < t1 || b.tFirst > t2))
                window.append(b);
        }

        // LOD missing / sparse for this viewport → raw MinMax
        if (window.size() < 2)
            return downsampleRaw(s, t1, t2, targetPoints, out);
        const double coverLo = window.first().tFirst;
        const double coverHi = window.last().tLast;
        const double pad = (t2 - t1) * 0.02;
        if (coverLo > t1 + pad || coverHi < t2 - pad)
            return downsampleRaw(s, t1, t2, targetPoints, out);
    }

    out->clear();
    if (window.isEmpty())
        return 0;
    return downsampleFromBuckets(window, t1, t2, targetPoints, out);
}

quint64 SampleStore::seqCounter(const SampleKey &key) const
{
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    return s ? s->seq : 0;
}

int SampleStore::sampleCount(const SampleKey &key) const
{
    QMutexLocker lock(&m_mutex);
    const Series *s = findSeries(key);
    return s ? s->ring.size() : 0;
}

SampleStore::Series *SampleStore::findSeries(const SampleKey &key)
{
    auto it = m_series.find(key);
    return it == m_series.end() ? nullptr : it.value();
}

const SampleStore::Series *SampleStore::findSeries(const SampleKey &key) const
{
    auto it = m_series.constFind(key);
    return it == m_series.constEnd() ? nullptr : it.value();
}

void SampleStore::rebuildIdIndex()
{
    m_idIndex.clear();
    for (auto it = m_series.begin(); it != m_series.end(); ++it) {
        Series *s = it.value();
        const quint64 idKey =
            (quint64(s->key.canId & 0x1FFFFFFF) << 1) | (s->key.extended ? 1ull : 0ull);
        m_idIndex[idKey].append(s);
    }
}

void SampleStore::pushOne(Series *s, double t, double v)
{
    if (s->ring.full() && s->ring.capacity() < m_capacity) {
        const int next = qMin(m_capacity, qMax(s->ring.capacity() * 2, 65536));
        s->ring.growTo(next);
    }
    s->ring.push({t, v});
    s->seq++;

    // Running Y range (P0-1) — no full scan on GUI
    if (!s->hasRange) {
        s->dataMin = s->dataMax = v;
        s->hasRange = true;
    } else {
        if (v < s->dataMin)
            s->dataMin = v;
        if (v > s->dataMax)
            s->dataMax = v;
    }

    // Incremental LOD bucket
    if (s->openCount == 0) {
        s->open.tFirst = s->open.tLast = t;
        s->open.vMin = s->open.vMax = v;
        s->open.tAtMin = s->open.tAtMax = t;
        s->openCount = 1;
    } else {
        s->open.tLast = t;
        if (v < s->open.vMin) {
            s->open.vMin = v;
            s->open.tAtMin = t;
        }
        if (v > s->open.vMax) {
            s->open.vMax = v;
            s->open.tAtMax = t;
        }
        s->openCount++;
    }

    if (s->openCount >= s->samplesPerBucket) {
        s->lod.push(s->open);
        s->openCount = 0;
    }
}
