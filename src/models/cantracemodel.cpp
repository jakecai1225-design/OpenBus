#include "cantracemodel.h"
#include "core/capturelog.h"
#include "core/filter_engine.h"
#include "core/dbcmanager.h"
#include "utils/canutils.h"

CanTraceModel::CanTraceModel(QObject *parent)
    : QAbstractTableModel(parent), m_ringBuffer(m_maxFrames)
{
    // Phase 2: batch flush timer
    m_flushTimer.setSingleShot(false);
    m_flushTimer.setInterval(static_cast<int>(m_refreshRate));
    connect(&m_flushTimer, &QTimer::timeout, this, [this]() {
        if (!m_pendingFrames.isEmpty())
            flushPending();
    });
    m_flushTimer.start();
}

// ============================================================
//  QAbstractTableModel
// ============================================================

int CanTraceModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return displaySize();
}

int CanTraceModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant CanTraceModel::data(const QModelIndex &index, int role) const
{
    const int n = displaySize();
    if (!index.isValid() || index.row() < 0 || index.row() >= n)
        return {};

    const int row = index.row();
    const bool inWindow = (row >= m_cacheFirst && row <= m_cacheLast && m_cacheFirst >= 0);

    // T4: prefer prefilled cache — no DBC decode / color-rule eval on paint.
    if (inWindow) {
        auto it = m_rowCache.constFind(row);
        if (it == m_rowCache.cend() || !it->colsFilled || !it->bgValid || !it->fgValid)
            fillRowCache(row);
        it = m_rowCache.constFind(row);
        if (it != m_rowCache.cend() && it->valid) {
            if (role == Qt::DisplayRole) {
                const QString &cached = it->cols[index.column()];
                if (!cached.isNull())
                    return cached;
            } else if (role == Qt::BackgroundRole) {
                if (it->bgValid)
                    return it->bgColor.isValid() ? QVariant(it->bgColor) : QVariant();
            } else if (role == Qt::ForegroundRole) {
                if (it->fgValid)
                    return it->fgColor;
            }
        }
    }

    CaptureFrameMeta meta;
    if (!frameMetaAt(row, &meta))
        return {};

    if (role == FrameRole)
        return QVariant::fromValue(frameWithPayload(row, meta));

    if (role == MarkedRole)
        return m_markedRows.contains(seqForRow(row));

    if (role == Qt::ToolTipRole) {
        auto labelIt = m_rowLabels.find(seqForRow(row));
        if (labelIt != m_rowLabels.end())
            return labelIt.value();
        return {};
    }

    if (role == Qt::TextAlignmentRole)
        return int(effectiveAlignment(index.column()));

    // Fallback outside window: meta-only colors (no rule/payload on paint).
    if (role == Qt::ForegroundRole) {
        if (meta.isErrorFrame())
            return QColor(0xD0, 0x20, 0x20);
        if (meta.direction() == CanFrame::Tx)
            return QColor(0x10, 0x50, 0xD0);
        return QColor(0x20, 0x20, 0x20);
    }

    if (role == Qt::BackgroundRole) {
        const quint64 seq = seqForRow(row);
        auto colorIt = m_rowColors.find(seq);
        if (colorIt != m_rowColors.end())
            return colorIt.value();
        if (m_markedRows.contains(seq))
            return QColor(0xFF, 0xF3, 0xB0);
        if (m_hasTimeRef && seq == m_timeRefSeq)
            return QColor(0xB2, 0xDF, 0xDB);
        if (m_errorFrameHighlight && meta.isErrorFrame())
            return QColor(0xFF, 0xCD, 0xD2);
        if (meta.fd())
            return QColor(0xE8, 0xF5, 0xE8);
        return {};
    }

    if (role == Qt::DisplayRole) {
        QString formatted;
        if (columnNeedsPayload(index.column())) {
            const CanFrame f = frameWithPayload(row, meta);
            formatCell(row, index.column(), f, formatted);
        } else {
            formatCellMeta(row, index.column(), meta, formatted);
        }
        return formatted;
    }

    return {};
}

QVariant CanTraceModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case ColNo:         return QStringLiteral("No.");
    case ColTime:       return QStringLiteral("Time");
    case ColDelta:      return QStringLiteral("Delta");
    case ColChannel:    return QStringLiteral("Ch");
    case ColDirection:  return QStringLiteral("Dir");
    case ColId:         return QStringLiteral("ID");
    case ColName:       return QStringLiteral("Name");
    case ColDlc:        return QStringLiteral("DLC");
    case ColData:       return QStringLiteral("Data");
    case ColFlags:      return QStringLiteral("Flags");
    case ColFrameCount: return QStringLiteral("Count");
    case ColInterval:   return QStringLiteral("Interval");
    case ColSignal:     return QStringLiteral("Signals");
    }
    return {};
}

// ============================================================
//  Phase 1: row format cache (meta-first for T1)
// ============================================================

bool CanTraceModel::columnNeedsPayload(int col)
{
    return col == ColData || col == ColSignal;
}

void CanTraceModel::formatCellMeta(int row, int col, const CaptureFrameMeta &m, QString &out) const
{
    switch (col) {
    case ColNo:
        out = QString::number(m_seqCounter - static_cast<quint64>(displaySize())
                              + static_cast<quint64>(row) + 1);
        return;
    case ColTime:
        if (m_hasTimeRef)
            out = CanUtils::formatTime(m.timestamp - m_timeRefTimestamp);
        else
            out = CanUtils::formatTime(m.timestamp);
        return;
    case ColDelta: {
        double prev = m.timestamp;
        if (row > 0) {
            CaptureFrameMeta prevMeta;
            if (frameMetaAt(row - 1, &prevMeta))
                prev = prevMeta.timestamp;
        }
        out = CanUtils::formatTime(m.timestamp - prev);
        return;
    }
    case ColChannel:
        out = QString::number(m.channel);
        return;
    case ColDirection:
        out = (m.direction() == CanFrame::Rx) ? QStringLiteral("Rx") : QStringLiteral("Tx");
        return;
    case ColId:
        out = CanUtils::formatId(m.id, m.extended());
        return;
    case ColName: {
        if (!m_dbcManager) {
            out = QStringLiteral("");
            return;
        }
        const DbcMessage *msg = m_dbcManager->findMessage(m.id);
        out = msg ? msg->name : QStringLiteral("");
        return;
    }
    case ColDlc:
        out = CanUtils::formatDlc(m.dlc, m.fd());
        return;
    case ColFlags:
        out = CanUtils::formatFlags(m.toFrameSkeleton());
        return;
    case ColFrameCount:
        out = QString::number(m_keyCount.value(overwriteKey(m.id, m.channel), 0));
        return;
    case ColInterval: {
        const quint64 key = overwriteKey(m.id, m.channel);
        auto it = m_intervalByKey.constFind(key);
        if (it == m_intervalByKey.constEnd())
            out = QStringLiteral("—");
        else
            out = CanUtils::formatTime(*it);
        return;
    }
    default:
        out = QStringLiteral("");
        return;
    }
}

void CanTraceModel::formatCell(int row, int col, const CanFrame &f, QString &out) const
{
    if (!columnNeedsPayload(col)) {
        formatCellMeta(row, col, CaptureFrameMeta::fromFrame(f), out);
        return;
    }
    switch (col) {
    case ColData:
        out = CanUtils::formatData(f.data);
        return;
    case ColSignal: {
        if (!m_dbcManager) {
            out = QStringLiteral("");
            return;
        }
        const DbcMessage *msg = m_dbcManager->findMessage(f.id);
        if (!msg) {
            out = QStringLiteral("");
            return;
        }
        auto decoded = m_dbcManager->decodeFrame(f.id, f.data);
        if (decoded.isEmpty()) {
            out = QStringLiteral("");
            return;
        }
        QStringList parts;
        for (const auto &sig : decoded) {
            QString val;
            if (!sig.valueDesc.isEmpty())
                val = sig.valueDesc;
            else
                val = QString::number(sig.physValue, 'g', 4);
            if (!sig.unit.isEmpty())
                val += sig.unit;
            parts.append(QStringLiteral("%1=%2").arg(sig.name).arg(val));
        }
        out = parts.join(QStringLiteral("  "));
        if (out.length() > 200)
            out = out.left(200) + QStringLiteral("...");
        return;
    }
    default:
        out = QStringLiteral("");
        return;
    }
}

void CanTraceModel::setVisibleRange(int first, int last)
{
    if (last < first) {
        m_cacheFirst = -1;
        m_cacheLast = -1;
        return;
    }

    // Keep a small pad so slight scrolls reuse filled rows.
    const int pad = 16;
    for (auto it = m_rowCache.begin(); it != m_rowCache.end();) {
        const int row = it.key();
        if (row < first - pad || row > last + pad)
            it = m_rowCache.erase(it);
        else
            ++it;
    }
    m_cacheFirst = first;
    m_cacheLast = last;
    prefillVisibleCache();
}

void CanTraceModel::invalidateRowCache()
{
    m_rowCache.clear();
    // Keep m_cacheFirst/Last; refill so paint stays cache-hit after sync/wrap.
    if (m_cacheFirst >= 0)
        prefillVisibleCache();
}

quint64 CanTraceModel::seqForRow(int row) const
{
    const int n = displaySize();
    if (row < 0 || row >= n)
        return 0;
    return m_seqCounter - static_cast<quint64>(n) + static_cast<quint64>(row);
}

void CanTraceModel::fillRowCache(int row) const
{
    const int n = displaySize();
    if (row < 0 || row >= n)
        return;

    RowCache &rc = m_rowCache[row];
    if (rc.colsFilled && rc.bgValid && rc.fgValid)
        return;

    CaptureFrameMeta meta;
    if (!frameMetaAt(row, &meta))
        return;

    CanFrame frame;
    bool haveFrame = false;
    auto ensureFrame = [&]() {
        if (!haveFrame) {
            frame = frameWithPayload(row, meta);
            haveFrame = true;
        }
    };

    if (!rc.colsFilled) {
        for (int c = 0; c < ColCount; ++c) {
            if (columnNeedsPayload(c)) {
                ensureFrame();
                formatCell(row, c, frame, rc.cols[c]);
            } else {
                formatCellMeta(row, c, meta, rc.cols[c]);
            }
        }
        rc.colsFilled = true;
    }

    const quint64 seq = seqForRow(row);

    if (!rc.fgValid) {
        rc.fgColor = QColor(0x20, 0x20, 0x20);
        if (!m_colorFilters.isEmpty()) {
            ensureFrame();
            const int ruleIdx = matchingColorRule(frame);
            if (ruleIdx >= 0 && m_colorRules[ruleIdx].foreground.isValid())
                rc.fgColor = m_colorRules[ruleIdx].foreground;
            else if (meta.isErrorFrame())
                rc.fgColor = QColor(0xD0, 0x20, 0x20);
            else if (meta.direction() == CanFrame::Tx)
                rc.fgColor = QColor(0x10, 0x50, 0xD0);
        } else if (meta.isErrorFrame()) {
            rc.fgColor = QColor(0xD0, 0x20, 0x20);
        } else if (meta.direction() == CanFrame::Tx) {
            rc.fgColor = QColor(0x10, 0x50, 0xD0);
        }
        rc.fgValid = true;
    }

    if (!rc.bgValid) {
        rc.bgColor = QColor(); // invalid = default brush
        auto colorIt = m_rowColors.constFind(seq);
        if (colorIt != m_rowColors.cend()) {
            rc.bgColor = colorIt.value();
        } else if (!m_colorFilters.isEmpty()) {
            ensureFrame();
            const int ruleIdx = matchingColorRule(frame);
            if (ruleIdx >= 0)
                rc.bgColor = m_colorRules[ruleIdx].background;
            else if (m_markedRows.contains(seq))
                rc.bgColor = QColor(0xFF, 0xF3, 0xB0);
            else if (m_hasTimeRef && seq == m_timeRefSeq)
                rc.bgColor = QColor(0xB2, 0xDF, 0xDB);
            else if (m_errorFrameHighlight && meta.isErrorFrame())
                rc.bgColor = QColor(0xFF, 0xCD, 0xD2);
            else if (meta.fd())
                rc.bgColor = QColor(0xE8, 0xF5, 0xE8);
        } else if (m_markedRows.contains(seq)) {
            rc.bgColor = QColor(0xFF, 0xF3, 0xB0);
        } else if (m_hasTimeRef && seq == m_timeRefSeq) {
            rc.bgColor = QColor(0xB2, 0xDF, 0xDB);
        } else if (m_errorFrameHighlight && meta.isErrorFrame()) {
            rc.bgColor = QColor(0xFF, 0xCD, 0xD2);
        } else if (meta.fd()) {
            rc.bgColor = QColor(0xE8, 0xF5, 0xE8);
        }
        rc.bgValid = true;
    }

    rc.valid = true;
}

void CanTraceModel::prefillVisibleCache() const
{
    if (m_cacheFirst < 0 || m_cacheLast < m_cacheFirst)
        return;
    const int n = displaySize();
    if (n <= 0)
        return;
    const int lo = qMax(0, m_cacheFirst);
    const int hi = qMin(n - 1, m_cacheLast);
    for (int r = lo; r <= hi; ++r)
        fillRowCache(r);
}

// ============================================================
//  Phase 2: 批量更新
// ============================================================

void CanTraceModel::setRefreshRate(RefreshRate rate)
{
    m_refreshRate = rate;
    if (rate == Paused) {
        m_flushTimer.stop();
    } else {
        m_flushTimer.setInterval(static_cast<int>(rate));
        if (!m_flushTimer.isActive())
            m_flushTimer.start();
    }
}

void CanTraceModel::flushPending()
{
    if (m_pendingFrames.isEmpty())
        return;
    if (m_captureCamera)
        leaveCaptureCameraForLocal();
    commitBatch(m_pendingFrames);
    m_pendingFrames.clear();
    emit framesCommitted(displaySize());
}

// ============================================================
//  Phase 4 / B5 data ops
// ============================================================

void CanTraceModel::setCaptureLogCamera(bool enabled)
{
    if (m_captureCamera == enabled)
        return;
    beginResetModel();
    m_captureCamera = enabled;
    m_pendingFrames.clear();
    m_ringBuffer.clear();
    if (!enabled)
        m_ringBuffer.reserve(m_maxFrames);
    else
        m_ringBuffer.reserve(1);
    m_viewRows = 0;
    m_seqCounter = 0;
    m_captureStartDateTime = QDateTime();
    m_keyToRow.clear();
    m_keyCount.clear();
    m_idCount.clear();
    m_lastTsByKey.clear();
    m_intervalByKey.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
    invalidateRowCache();
    endResetModel();
}

void CanTraceModel::leaveCaptureCameraForLocal()
{
    if (!m_captureCamera)
        return;
    beginResetModel();
    m_captureCamera = false;
    m_viewRows = 0;
    m_seqCounter = 0;
    m_ringBuffer.clear();
    m_ringBuffer.reserve(m_maxFrames);
    m_pendingFrames.clear();
    m_keyToRow.clear();
    m_keyCount.clear();
    m_idCount.clear();
    m_lastTsByKey.clear();
    m_intervalByKey.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
    m_captureStartDateTime = QDateTime();
    invalidateRowCache();
    endResetModel();
}

int CanTraceModel::syncFromCaptureLog(quint64 *cursorSeq, int maxRows)
{
    if (!m_captureCamera || !cursorSeq)
        return 0;

    quint64 newSeq = *cursorSeq;
    const int cap = maxRows > 0 ? maxRows : 256;
    const int visited = CaptureLog::instance()->visitAfterSeq(
        *cursorSeq,
        [this](const CanFrame &f) {
            m_idCount[f.id]++;
            m_keyCount[overwriteKey(f.id, f.channel)]++;
        },
        &newSeq,
        cap);

    int logSize = 0;
    quint64 logSeq = 0;
    CaptureLog::instance()->snapshot(&logSize, &logSeq, nullptr);

    int size = 0;
    if (newSeq > logSeq - static_cast<quint64>(logSize))
        size = static_cast<int>(newSeq - (logSeq - static_cast<quint64>(logSize)));
    size = qBound(0, size, logSize);
    const quint64 seq = newSeq;

    const int oldSize = m_viewRows;
    const quint64 oldSeq = m_seqCounter;
    const quint64 oldBase = oldSeq - static_cast<quint64>(oldSize);
    const quint64 newBase = seq - static_cast<quint64>(size);

    if (oldSeq == 0 && seq > 0 && !m_captureStartDateTime.isValid())
        m_captureStartDateTime = QDateTime::currentDateTime();

    *cursorSeq = newSeq;

    if (size == oldSize && seq == oldSeq)
        return 0;

    if (seq < oldSeq || (size == 0 && oldSize > 0 && seq == 0)) {
        beginResetModel();
        m_viewRows = size;
        m_seqCounter = seq;
        if (size == 0) {
            m_idCount.clear();
            m_keyCount.clear();
            m_keyToRow.clear();
            m_lastTsByKey.clear();
            m_intervalByKey.clear();
            m_markedRows.clear();
            m_rowColors.clear();
            m_rowLabels.clear();
            m_hasTimeRef = false;
            m_timeRefSeq = 0;
            m_timeRefTimestamp = 0.0;
            m_captureStartDateTime = QDateTime();
        }
        invalidateRowCache();
        endResetModel();
        emit framesCommitted(size);
        return visited;
    }

    if (newBase > oldBase) {
        const quint64 shift64 = newBase - oldBase;
        const int shift = static_cast<int>(qMin<quint64>(shift64, static_cast<quint64>(INT_MAX)));
        m_viewRows = size;
        m_seqCounter = seq;
        invalidateRowCache();
        emit ringWrapped(shift);
        // Do not emit dataChanged over the full history — ViewportProxy maps the
        // visible window; ringWrapped / follow-end refresh covers the table.
    } else if (size > oldSize) {
        beginInsertRows({}, oldSize, size - 1);
        m_viewRows = size;
        m_seqCounter = seq;
        endInsertRows();
    } else if (seq > oldSeq) {
        m_viewRows = size;
        m_seqCounter = seq;
        invalidateRowCache();
        emit ringWrapped(static_cast<int>(qMin<quint64>(seq - oldSeq,
                                                         static_cast<quint64>(INT_MAX))));
    } else {
        m_viewRows = size;
        m_seqCounter = seq;
        invalidateRowCache();
    }

    emit framesCommitted(size);
    return visited;
}

void CanTraceModel::adoptCaptureLogSnapshot(int size, quint64 seq)
{
    if (!m_captureCamera)
        return;
    size = qMax(0, size);
    beginResetModel();
    m_viewRows = size;
    m_seqCounter = seq;
    if (size == 0) {
        m_idCount.clear();
        m_keyCount.clear();
        m_keyToRow.clear();
        m_lastTsByKey.clear();
        m_intervalByKey.clear();
        m_markedRows.clear();
        m_rowColors.clear();
        m_rowLabels.clear();
        m_hasTimeRef = false;
        m_timeRefSeq = 0;
        m_timeRefTimestamp = 0.0;
        m_captureStartDateTime = QDateTime();
    } else if (!m_captureStartDateTime.isValid()) {
        m_captureStartDateTime = QDateTime::currentDateTime();
    }
    invalidateRowCache();
    endResetModel();
    emit framesCommitted(size);
}

void CanTraceModel::appendFrame(const CanFrame &frame)
{
    if (m_captureCamera)
        leaveCaptureCameraForLocal();
    m_pendingFrames.append(frame);
    if (m_pendingFrames.size() >= pendingSoftCap())
        flushPending();
}

void CanTraceModel::enqueueFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;
    if (m_captureCamera)
        leaveCaptureCameraForLocal();
    m_pendingFrames.reserve(m_pendingFrames.size() + frames.size());
    m_pendingFrames += frames;
    if (m_pendingFrames.size() >= pendingSoftCap())
        flushPending();
}

void CanTraceModel::appendFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;
    if (m_captureCamera)
        leaveCaptureCameraForLocal();
    commitBatch(frames);
    emit framesCommitted(displaySize());
}

void CanTraceModel::commitBatch(const QVector<CanFrame> &frames)
{
    if (m_overwriteMode) {
        // Collapse chronologically: count every frame, keep only the latest
        // payload per id+channel, then one dataChanged for touched rows.
        // Avoids per-frame dataChanged (was starving the UI under high load).
        int lo = -1;
        int hi = -1;
        auto noteRow = [&](int row) {
            if (lo < 0) {
                lo = hi = row;
            } else {
                lo = qMin(lo, row);
                hi = qMax(hi, row);
            }
        };

        for (const auto &f : frames) {
            const quint64 key = overwriteKey(f.id, f.channel);
            m_idCount[f.id]++;
            m_keyCount[key]++;

            auto lastIt = m_lastTsByKey.find(key);
            if (lastIt != m_lastTsByKey.end())
                m_intervalByKey[key] = f.timestamp - lastIt.value();
            m_lastTsByKey[key] = f.timestamp;

            auto it = m_keyToRow.find(key);
            if (it != m_keyToRow.end()) {
                const int row = it.value();
                m_ringBuffer.at(row) = f;
                noteRow(row);
                continue;
            }
            if (!m_ringBuffer.full()) {
                const int row = m_ringBuffer.size();
                beginInsertRows({}, row, row);
                m_ringBuffer.push(f);
                m_seqCounter++;
                m_keyToRow.insert(key, row);
                endInsertRows();
                noteRow(row);
            } else {
                m_ringBuffer.push(f);
                m_seqCounter++;
                const int row = m_ringBuffer.size() - 1;
                m_keyToRow[key] = row;
                noteRow(row);
            }
        }

        invalidateRowCache();
        if (lo >= 0)
            emit dataChanged(index(lo, 0), index(hi, ColCount - 1));
        return;
    }

    // 滚动模式：逐帧 push（环形缓冲区自动处理覆盖）
    int oldSize = m_ringBuffer.size();
    int batchSize = frames.size();

    // Accumulate counts (id-only + overwrite key)
    for (const auto &f : frames) {
        m_idCount[f.id]++;
        m_keyCount[overwriteKey(f.id, f.channel)]++;
    }

    if (!m_ringBuffer.full()) {
        // 缓冲区未满：逐帧 push，批量通知插入
        int remaining = m_ringBuffer.capacity() - oldSize;
        if (batchSize <= remaining) {
            // 全部可以插入，不触发覆盖
            int first = oldSize;
            int last = first + batchSize - 1;
            beginInsertRows({}, first, last);
            for (const auto &f : frames)
                m_ringBuffer.push(f);
            m_seqCounter += batchSize;
            endInsertRows();
        } else {
            // 先插入未满部分
            beginInsertRows({}, oldSize, m_ringBuffer.capacity() - 1);
            for (int i = 0; i < remaining; ++i)
                m_ringBuffer.push(frames[i]);
            m_seqCounter += remaining;
            endInsertRows();
            // 剩余帧逐帧覆盖
            for (int i = remaining; i < batchSize; ++i) {
                m_ringBuffer.push(frames[i]);
                m_seqCounter++;
            }
            emit ringWrapped(batchSize - remaining);
        }
    } else {
        // Buffer already full: overwrite oldest
        for (const auto &f : frames) {
            m_ringBuffer.push(f);
            m_seqCounter++;
        }
        emit ringWrapped(batchSize);
    }

    invalidateRowCache();
}

void CanTraceModel::commitFrame(const CanFrame &frame)
{
    // First commit records wall-clock capture start
    if (m_seqCounter == 0)
        m_captureStartDateTime = QDateTime::currentDateTime();

    const quint64 key = overwriteKey(frame.id, frame.channel);
    m_idCount[frame.id]++;
    m_keyCount[key]++;

    // O(1) cycle interval for overwrite-key (and Count/Interval columns)
    auto lastIt = m_lastTsByKey.find(key);
    if (lastIt != m_lastTsByKey.end())
        m_intervalByKey[key] = frame.timestamp - lastIt.value();
    m_lastTsByKey[key] = frame.timestamp;

    if (m_overwriteMode) {
        auto it = m_keyToRow.find(key);
        if (it != m_keyToRow.end()) {
            int row = it.value();
            m_ringBuffer.at(row) = frame;
            emit dataChanged(index(row, 0), index(row, ColCount - 1));
            return;
        }
        // New id+channel: append fixed row
        int row = m_ringBuffer.size();
        if (!m_ringBuffer.full()) {
            beginInsertRows({}, row, row);
            m_ringBuffer.push(frame);
            m_seqCounter++;
            m_keyToRow[key] = row;
            endInsertRows();
        } else {
            // Ring full: overwrite oldest slot
            m_ringBuffer.push(frame);
            m_seqCounter++;
            m_keyToRow[key] = m_ringBuffer.size() - 1;
            emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1));
        }
        return;
    }

    // Scroll mode
    if (!m_ringBuffer.full()) {
        int row = m_ringBuffer.size();
        beginInsertRows({}, row, row);
        m_ringBuffer.push(frame);
        m_seqCounter++;
        endInsertRows();
    } else {
        m_ringBuffer.push(frame);
        m_seqCounter++;
        invalidateRowCache();
        emit ringWrapped(1);
    }
}

void CanTraceModel::clear()
{
    beginResetModel();
    m_ringBuffer.clear();
    m_viewRows = 0;
    m_seqCounter = 0;
    m_captureStartDateTime = QDateTime();
    m_keyToRow.clear();
    m_keyCount.clear();
    m_idCount.clear();
    m_lastTsByKey.clear();
    m_intervalByKey.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
    m_pendingFrames.clear();
    invalidateRowCache();
    endResetModel();
}

void CanTraceModel::setMaxFrames(int max)
{
    m_maxFrames = max;
    beginResetModel();
    if (m_captureCamera)
        m_ringBuffer.reserve(1);
    else
        m_ringBuffer.reserve(max);
    m_viewRows = 0;
    m_seqCounter = 0;
    m_captureStartDateTime = QDateTime();
    m_keyToRow.clear();
    m_keyCount.clear();
    m_idCount.clear();
    m_lastTsByKey.clear();
    m_intervalByKey.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
    invalidateRowCache();
    endResetModel();
}

CanFrame CanTraceModel::frameAt(int row) const
{
    if (row < 0 || row >= displaySize())
        return {};
    if (m_captureCamera) {
        CanFrame f;
        if (!CaptureLog::instance()->frameAt(row, &f))
            return {};
        return f;
    }
    return m_ringBuffer.at(row);
}

bool CanTraceModel::frameMetaAt(int row, CaptureFrameMeta *out) const
{
    if (!out || row < 0 || row >= displaySize())
        return false;
    if (m_captureCamera)
        return CaptureLog::instance()->frameMetaAt(row, out);
    *out = CaptureFrameMeta::fromFrame(m_ringBuffer.at(row));
    return true;
}

CanFrame CanTraceModel::frameWithPayload(int row, const CaptureFrameMeta &meta) const
{
    CanFrame f = meta.toFrameSkeleton();
    if (m_captureCamera) {
        CaptureLog::instance()->copyDataAt(row, &f.data);
        return f;
    }
    if (row >= 0 && row < m_ringBuffer.size())
        f.data = m_ringBuffer.at(row).data;
    return f;
}

QVector<CanFrame> CanTraceModel::frames() const
{
    if (m_captureCamera) {
        QVector<CanFrame> result;
        CaptureLog::instance()->copyRange(0, m_viewRows, &result);
        return result;
    }
    QVector<CanFrame> result;
    result.reserve(m_ringBuffer.size());
    for (int i = 0; i < m_ringBuffer.size(); ++i)
        result.append(m_ringBuffer.at(i));
    return result;
}

// ============================================================
//  唯一值收集（Excel 风格筛选面板）
// ============================================================

QList<QPair<QString, int>> CanTraceModel::uniqueValues(int column) const
{
    QList<QPair<QString, int>> result;
    int total = displaySize();
    if (total == 0)
        return result;

    // ID column: use m_idCount
    if (column == ColId) {
        for (auto it = m_idCount.constBegin(); it != m_idCount.constEnd(); ++it) {
            bool extended = false;
            for (int i = 0; i < total; ++i) {
                const CanFrame f = frameAt(i);
                if (f.id == it.key()) {
                    extended = f.extended;
                    break;
                }
            }
            result.append({CanUtils::formatId(it.key(), extended), (int)it.value()});
        }
        std::sort(result.begin(), result.end(),
                  [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                      return a.first.toLower() < b.first.toLower();
                  });
        return result;
    }

    QHash<QString, int> valueCounts;
    QString tmp;
    for (int i = 0; i < total; ++i) {
        const CanFrame f = frameAt(i);
        formatCell(i, column, f, tmp);
        valueCounts[tmp]++;
    }

    for (auto it = valueCounts.constBegin(); it != valueCounts.constEnd(); ++it)
        result.append({it.key(), it.value()});

    std::sort(result.begin(), result.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  return a.first.toLower() < b.first.toLower();
              });

    return result;
}

// ============================================================
//  行标记与着色
// ============================================================

void CanTraceModel::toggleMark(int row)
{
    if (row < 0 || row >= displaySize())
        return;
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    if (m_markedRows.contains(seq))
        m_markedRows.remove(seq);
    else
        m_markedRows.insert(seq);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, MarkedRole});
}

void CanTraceModel::setMarked(int row, bool marked)
{
    if (row < 0 || row >= displaySize())
        return;
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    if (marked)
        m_markedRows.insert(seq);
    else
        m_markedRows.remove(seq);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, MarkedRole});
}

bool CanTraceModel::isMarked(int row) const
{
    if (row < 0 || row >= displaySize())
        return false;
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    return m_markedRows.contains(seq);
}

void CanTraceModel::clearMarks()
{
    m_markedRows.clear();
    if (displaySize() > 0)
        emit dataChanged(index(0, 0), index(displaySize() - 1, ColCount - 1),
                         {Qt::BackgroundRole, MarkedRole});
}

QList<int> CanTraceModel::markedRows() const
{
    QList<int> result;
    int size = displaySize();
    for (int row = 0; row < size; ++row) {
        quint64 seq = m_seqCounter - static_cast<quint64>(size) + static_cast<quint64>(row);
        if (m_markedRows.contains(seq))
            result.append(row);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void CanTraceModel::setRowColor(int row, const QColor &color)
{
    if (row < 0 || row >= displaySize())
        return;
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    if (color.isValid())
        m_rowColors[seq] = color;
    else
        m_rowColors.remove(seq);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole});
}

QColor CanTraceModel::rowColor(int row) const
{
    if (row < 0 || row >= displaySize())
        return {};
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    return m_rowColors.value(seq);
}

void CanTraceModel::clearColors()
{
    m_rowColors.clear();
    if (displaySize() > 0)
        emit dataChanged(index(0, 0), index(displaySize() - 1, ColCount - 1),
                         {Qt::BackgroundRole});
}

// ============================================================
//  时间参考点
// ============================================================

void CanTraceModel::setTimeReference(int row)
{
    if (row < 0 || row >= displaySize())
        return;
    quint64 oldSeq = m_timeRefSeq;
    m_timeRefSeq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    m_timeRefTimestamp = frameAt(row).timestamp;
    m_hasTimeRef = true;
    invalidateRowCache();
    if (displaySize() > 0) {
        emit dataChanged(index(0, 0), index(displaySize() - 1, ColCount - 1),
                         {Qt::DisplayRole, Qt::BackgroundRole});
        Q_UNUSED(oldSeq);
    }
}

void CanTraceModel::clearTimeReference()
{
    if (!m_hasTimeRef)
        return;
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
    invalidateRowCache();
    if (displaySize() > 0)
        emit dataChanged(index(0, 0), index(displaySize() - 1, ColCount - 1),
                         {Qt::DisplayRole, Qt::BackgroundRole});
}

int CanTraceModel::timeReferenceRow() const
{
    if (!m_hasTimeRef)
        return -1;
    int row = static_cast<int>(m_timeRefSeq) - static_cast<int>(m_seqCounter - static_cast<quint64>(displaySize()));
    if (row < 0 || row >= displaySize())
        return -1;
    return row;
}
// ============================================================
//  行标签（Notepad++ 风格书签）
// ============================================================

void CanTraceModel::setRowLabel(int row, const QString &label)
{
    if (row < 0 || row >= displaySize())
        return;
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    if (label.isEmpty())
        m_rowLabels.remove(seq);
    else
        m_rowLabels[seq] = label;
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, Qt::ToolTipRole});
}

QString CanTraceModel::rowLabel(int row) const
{
    if (row < 0 || row >= displaySize())
        return {};
    quint64 seq = m_seqCounter - static_cast<quint64>(displaySize()) + static_cast<quint64>(row);
    return m_rowLabels.value(seq);
}

QList<QPair<int, QString>> CanTraceModel::labeledMarks() const
{
    QList<QPair<int, QString>> result;
    int size = displaySize();
    for (int row = 0; row < size; ++row) {
        quint64 seq = m_seqCounter - static_cast<quint64>(size) + static_cast<quint64>(row);
        if (m_markedRows.contains(seq) || m_rowColors.contains(seq) || m_rowLabels.contains(seq)) {
            QString label = m_rowLabels.value(seq,
                QString("#%1").arg(row + 1));
            result.append(qMakePair(row, label));
        }
    }
    return result;
}

void CanTraceModel::clearLabels()
{
    m_rowLabels.clear();
    if (displaySize() > 0)
        emit dataChanged(index(0, 0), index(displaySize() - 1, ColCount - 1),
                         {Qt::BackgroundRole, Qt::ToolTipRole});
}

// ============================================================
//  着色规则
// ============================================================

void CanTraceModel::setColorRules(const QVector<ColorRule> &rules)
{
    for (auto *fe : m_colorFilters)
        delete fe;
    m_colorFilters.clear();
    m_colorRules = rules;

    // 与规则下标一一对应：禁用/编译失败的槽位置 nullptr，
    // 避免“过滤器列表与规则列表下标错位”导致颜色张冠李戴
    // （编辑器保存时应已校验表达式；此处对编译失败规则静默降级为不生效）
    for (const auto &r : rules) {
        if (!r.enabled) {
            m_colorFilters.append(nullptr);
            continue;
        }
        auto *fe = new FilterEngine();
        if (fe->compile(r.expr))
            m_colorFilters.append(fe);
        else {
            delete fe;
            m_colorFilters.append(nullptr);
        }
    }

    invalidateRowCache();
    if (m_cacheFirst >= 0 && displaySize() > 0) {
        const int lo = qMax(0, m_cacheFirst);
        const int hi = qMin(displaySize() - 1, m_cacheLast);
        if (lo <= hi)
            emit dataChanged(index(lo, 0), index(hi, ColCount - 1),
                             {Qt::BackgroundRole, Qt::ForegroundRole, Qt::DisplayRole});
    }
}

void CanTraceModel::clearColorRules()
{
    for (auto *fe : m_colorFilters)
        delete fe;
    m_colorFilters.clear();
    m_colorRules.clear();

    invalidateRowCache();
    if (m_cacheFirst >= 0 && displaySize() > 0) {
        const int lo = qMax(0, m_cacheFirst);
        const int hi = qMin(displaySize() - 1, m_cacheLast);
        if (lo <= hi)
            emit dataChanged(index(lo, 0), index(hi, ColCount - 1),
                             {Qt::BackgroundRole, Qt::ForegroundRole, Qt::DisplayRole});
    }
}

int CanTraceModel::matchingColorRule(const CanFrame &frame) const
{
    for (int i = 0; i < m_colorFilters.size(); ++i) {
        if (m_colorFilters[i] && m_colorFilters[i]->evaluate(frame))
            return i;
    }
    return -1;
}

// ============================================================
//  DBC manager
// ============================================================

void CanTraceModel::setDbcManager(DbcManager *mgr)
{
    m_dbcManager = mgr;
    invalidateRowCache();
    if (m_cacheFirst >= 0 && displaySize() > 0) {
        const int lo = qMax(0, m_cacheFirst);
        const int hi = qMin(displaySize() - 1, m_cacheLast);
        if (lo <= hi)
            emit dataChanged(index(lo, 0), index(hi, ColCount - 1),
                             {Qt::DisplayRole});
    }
}

// ============================================================
//  Overwrite mode
// ============================================================

void CanTraceModel::setOverwriteMode(bool mode)
{
    if (m_overwriteMode == mode)
        return;

    if (!mode) {
        m_overwriteMode = false;
        m_keyToRow.clear();
        return;
    }

    // Snapshot current view first (camera frameAt still works). Leaving the
    // camera used to clear the ring and leave a blank Trace until new frames
    // arrived — seed unique id+channel rows from what the user already sees.
    const int n = displaySize();
    QVector<CanFrame> all;
    all.reserve(n);
    for (int i = 0; i < n; ++i)
        all.append(frameAt(i));

    beginResetModel();
    m_captureCamera = false;
    m_overwriteMode = true;
    m_pendingFrames.clear();
    m_ringBuffer.clear();
    m_ringBuffer.reserve(m_maxFrames);
    m_viewRows = 0;
    m_seqCounter = 0;
    m_keyToRow.clear();
    m_keyCount.clear();
    m_idCount.clear();
    m_lastTsByKey.clear();
    m_intervalByKey.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
    if (!m_captureStartDateTime.isValid() && !all.isEmpty())
        m_captureStartDateTime = QDateTime::currentDateTime();

    for (const CanFrame &f : all) {
        const quint64 key = overwriteKey(f.id, f.channel);
        m_idCount[f.id]++;
        m_keyCount[key]++;
        auto lit = m_lastTsByKey.find(key);
        if (lit != m_lastTsByKey.end())
            m_intervalByKey[key] = f.timestamp - lit.value();
        m_lastTsByKey[key] = f.timestamp;

        auto it = m_keyToRow.find(key);
        if (it != m_keyToRow.end()) {
            m_ringBuffer.at(it.value()) = f;
        } else if (!m_ringBuffer.full()) {
            m_keyToRow.insert(key, m_ringBuffer.size());
            m_ringBuffer.push(f);
            m_seqCounter++;
        }
    }

    invalidateRowCache();
    endResetModel();
    emit framesCommitted(displaySize());
}

// ============================================================
//  错误帧高亮
// ============================================================

void CanTraceModel::setErrorFrameHighlight(bool enabled)
{
    if (m_errorFrameHighlight == enabled)
        return;
    m_errorFrameHighlight = enabled;
    invalidateRowCache();
    if (displaySize() > 0)
        emit dataChanged(index(0, 0), index(displaySize() - 1, ColCount - 1),
                         {Qt::BackgroundRole});
}

// ============================================================
//  Column alignment
// ============================================================

static const QHash<int, Qt::Alignment> &getDefaultAlignments()
{
    static QHash<int, Qt::Alignment> defaultMap = []() {
        QHash<int, Qt::Alignment> h;
        // 数字列：右对齐 + 垂直居中
        h[CanTraceModel::ColNo]     = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColTime]   = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColDelta]  = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColChannel]      = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColId]       = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColDlc]    = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColFrameCount] = Qt::AlignRight | Qt::AlignVCenter;
        h[CanTraceModel::ColInterval]   = Qt::AlignRight | Qt::AlignVCenter;
        // 文本列：左对齐 + 垂直居中
        h[CanTraceModel::ColName]   = Qt::AlignLeft | Qt::AlignVCenter;
        h[CanTraceModel::ColData]   = Qt::AlignLeft | Qt::AlignVCenter;
        h[CanTraceModel::ColSignal] = Qt::AlignLeft | Qt::AlignVCenter;
        // 标签列：居中对齐 + 垂直居中
        h[CanTraceModel::ColDirection] = Qt::AlignHCenter | Qt::AlignVCenter;
        h[CanTraceModel::ColFlags]     = Qt::AlignHCenter | Qt::AlignVCenter;
        return h;
    }();
    return defaultMap;
}

Qt::Alignment CanTraceModel::effectiveAlignment(int column) const
{
    if (m_columnAlignments.contains(column))
        return m_columnAlignments.value(column);
    return getDefaultAlignments().value(column);
}

void CanTraceModel::setColumnAlignment(int col, Qt::Alignment align)
{
    if (col < 0 || col >= ColCount)
        return;
    m_columnAlignments[col] = align;
    emit layoutChanged();
}

Qt::Alignment CanTraceModel::columnAlignment(int col) const
{
    if (m_columnAlignments.contains(col))
        return m_columnAlignments.value(col);
    return getDefaultAlignments().value(col);
}

void CanTraceModel::resetToDefault(int col)
{
    if (col >= 0 && col < ColCount)
        m_columnAlignments.remove(col);
    emit layoutChanged();
}
