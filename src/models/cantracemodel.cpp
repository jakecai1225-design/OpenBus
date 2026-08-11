#include "cantracemodel.h"
#include "core/filter_engine.h"
#include "utils/canutils.h"

CanTraceModel::CanTraceModel(QObject *parent)
    : QAbstractTableModel(parent), m_ringBuffer(m_maxFrames)
{
    // Phase 2: 批量刷新定时器
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
    return m_ringBuffer.size();
}

int CanTraceModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return ColCount;
}

QVariant CanTraceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ringBuffer.size())
        return {};

    const CanFrame &f = m_ringBuffer.at(index.row());

    if (role == FrameRole)
        return QVariant::fromValue(f);

    if (role == MarkedRole) {
        quint64 seq = m_seqCounter - m_ringBuffer.size() + index.row();
        return m_markedRows.contains(seq);
    }

    if (role == Qt::ToolTipRole) {
        quint64 seq = m_seqCounter - m_ringBuffer.size() + index.row();
        auto labelIt = m_rowLabels.find(seq);
        if (labelIt != m_rowLabels.end())
            return labelIt.value();
    }

    if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
        case ColNo: case ColTime: case ColDelta:
        case ColId: case ColDlc: case ColFrameCount:
            return int(Qt::AlignRight | Qt::AlignVCenter);
        case ColData:
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        default:
            return int(Qt::AlignCenter);
        }
    }

    if (role == Qt::ForegroundRole) {
        if (f.isErrorFrame())
            return QColor(0xD0, 0x20, 0x20);
        if (f.direction == CanFrame::Tx)
            return QColor(0x10, 0x50, 0xD0);
        return QColor(0x20, 0x20, 0x20);
    }

    if (role == Qt::BackgroundRole) {
        quint64 seq = m_seqCounter - m_ringBuffer.size() + index.row();
        // 用户自定义颜色
        auto colorIt = m_rowColors.find(seq);
        if (colorIt != m_rowColors.end())
            return colorIt.value();
        // Phase 1: 着色规则结果从行缓存获取
        if (index.row() >= m_cacheFirst && index.row() <= m_cacheLast) {
            auto it = m_rowCache.find(index.row());
            if (it != m_rowCache.end() && it->valid && it->bgValid)
                return it->bgColor;
        }
        // 未命中缓存，实时求值
        QColor ruleColor = evaluateColorRules(f);
        if (ruleColor.isValid())
            return ruleColor;
        if (m_markedRows.contains(seq))
            return QColor(0xFF, 0xF3, 0xB0);
        if (f.fd)
            return QColor(0xE8, 0xF5, 0xE8);
        return {};
    }

    if (role == Qt::DisplayRole) {
        // Phase 1: 先查行缓存
        if (index.row() >= m_cacheFirst && index.row() <= m_cacheLast) {
            auto it = m_rowCache.find(index.row());
            if (it != m_rowCache.end() && it->valid) {
                const QString &cached = it->cols[index.column()];
                if (!cached.isNull())
                    return cached;
            }
        }
        // 未命中缓存，格式化并写入缓存
        QString formatted;
        formatCell(index.row(), index.column(), f, formatted);
        // 仅在可见行范围内写入缓存
        if (index.row() >= m_cacheFirst && index.row() <= m_cacheLast) {
            RowCache &rc = m_rowCache[index.row()];
            rc.cols[index.column()] = formatted;
            rc.valid = true;
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
    case ColDlc:        return QStringLiteral("DLC");
    case ColData:       return QStringLiteral("Data");
    case ColFlags:      return QStringLiteral("Flags");
    case ColFrameCount: return QStringLiteral("Count");
    }
    return {};
}

// ============================================================
//  Phase 1: 行缓存格式化
// ============================================================

void CanTraceModel::formatCell(int row, int col, const CanFrame &f, QString &out) const
{
    switch (col) {
    case ColNo:
        out = QString::number(m_seqCounter - m_ringBuffer.size() + row + 1);
        return;
    case ColTime:
        out = CanUtils::formatTime(f.timestamp);
        return;
    case ColDelta: {
        double prev = (row > 0) ? m_ringBuffer.at(row - 1).timestamp : f.timestamp;
        out = CanUtils::formatTime(f.timestamp - prev);
        return;
    }
    case ColChannel:
        out = QString::number(f.channel);
        return;
    case ColDirection:
        out = (f.direction == CanFrame::Rx) ? "Rx" : "Tx";
        return;
    case ColId:
        out = CanUtils::formatId(f.id, f.extended);
        return;
    case ColDlc:
        out = CanUtils::formatDlc(f.dlc, f.fd);
        return;
    case ColData:
        out = CanUtils::formatData(f.data);
        return;
    case ColFlags:
        out = CanUtils::formatFlags(f);
        return;
    case ColFrameCount:
        out = QString::number(m_idCount.value(f.id, 0));
        return;
    }
}

void CanTraceModel::setVisibleRange(int first, int last)
{
    // 淘汰范围外的缓存
    for (auto it = m_rowCache.begin(); it != m_rowCache.end();) {
        int row = it.key();
        if (row < first - 50 || row > last + 50)
            it = m_rowCache.erase(it);
        else
            ++it;
    }
    m_cacheFirst = first;
    m_cacheLast = last;
}

void CanTraceModel::invalidateRowCache()
{
    m_rowCache.clear();
    m_cacheFirst = -1;
    m_cacheLast = -1;
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
    commitBatch(m_pendingFrames);
    m_pendingFrames.clear();
    emit framesCommitted(m_ringBuffer.size());
}

// ============================================================
//  Phase 4: 环形缓冲区数据操作
// ============================================================

void CanTraceModel::appendFrame(const CanFrame &frame)
{
    m_pendingFrames.append(frame);
}

void CanTraceModel::appendFrames(const QVector<CanFrame> &frames)
{
    if (frames.isEmpty())
        return;
    // 离线加载：直接批量提交，不经过 pending 队列
    commitBatch(frames);
    emit framesCommitted(m_ringBuffer.size());
}

void CanTraceModel::commitBatch(const QVector<CanFrame> &frames)
{
    if (m_overwriteMode) {
        // 覆盖模式：逐帧处理
        for (const auto &f : frames)
            commitFrame(f);
        invalidateRowCache();
        return;
    }

    // 滚动模式：逐帧 push（环形缓冲区自动处理覆盖）
    int oldSize = m_ringBuffer.size();
    int batchSize = frames.size();

    // 累计 ID 计数（一次性）
    for (const auto &f : frames)
        m_idCount[f.id]++;

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
            // 批量通知数据移动
            emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                             {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole, MarkedRole});
        }
    } else {
        // 缓冲区已满：逐帧覆盖
        for (const auto &f : frames) {
            m_ringBuffer.push(f);
            m_seqCounter++;
        }
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole, MarkedRole});
    }

    invalidateRowCache();
}

void CanTraceModel::commitFrame(const CanFrame &frame)
{
    m_idCount[frame.id]++;

    if (m_overwriteMode) {
        auto it = m_idToRow.find(frame.id);
        if (it != m_idToRow.end()) {
            int row = it.value();
            m_ringBuffer.at(row) = frame;
            emit dataChanged(index(row, 0), index(row, ColCount - 1));
            return;
        }
        // 新 CAN ID：追加
        int row = m_ringBuffer.size();
        if (!m_ringBuffer.full()) {
            beginInsertRows({}, row, row);
            m_ringBuffer.push(frame);
            m_seqCounter++;
            m_idToRow[frame.id] = row;
            endInsertRows();
        } else {
            // 满了，覆盖最旧帧
            m_ringBuffer.push(frame);
            m_seqCounter++;
            m_idToRow[frame.id] = m_ringBuffer.size() - 1;
            emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1));
        }
        return;
    }

    // 滚动模式
    if (!m_ringBuffer.full()) {
        int row = m_ringBuffer.size();
        beginInsertRows({}, row, row);
        m_ringBuffer.push(frame);
        m_seqCounter++;
        endInsertRows();
    } else {
        // 缓冲区满：覆盖最旧帧，O(1)
        m_ringBuffer.push(frame);
        m_seqCounter++;
        // 所有逻辑行数据已移动，通知视图刷新可见行
        // 清除行缓存（数据位置已变化）
        invalidateRowCache();
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole, MarkedRole});
    }
}

void CanTraceModel::clear()
{
    beginResetModel();
    m_ringBuffer.clear();
    m_seqCounter = 0;
    m_idToRow.clear();
    m_idCount.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_pendingFrames.clear();
    invalidateRowCache();
    endResetModel();
}

void CanTraceModel::setMaxFrames(int max)
{
    m_maxFrames = max;
    beginResetModel();
    m_ringBuffer.reserve(max);
    m_seqCounter = 0;
    m_idToRow.clear();
    m_idCount.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    invalidateRowCache();
    endResetModel();
}

const CanFrame &CanTraceModel::frameAt(int row) const
{
    return m_ringBuffer.at(row);
}

QVector<CanFrame> CanTraceModel::frames() const
{
    QVector<CanFrame> result;
    result.reserve(m_ringBuffer.size());
    for (int i = 0; i < m_ringBuffer.size(); ++i)
        result.append(m_ringBuffer.at(i));
    return result;
}

// ============================================================
//  行标记与着色
// ============================================================

void CanTraceModel::toggleMark(int row)
{
    if (row < 0 || row >= m_ringBuffer.size())
        return;
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    if (m_markedRows.contains(seq))
        m_markedRows.remove(seq);
    else
        m_markedRows.insert(seq);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, MarkedRole});
}

void CanTraceModel::setMarked(int row, bool marked)
{
    if (row < 0 || row >= m_ringBuffer.size())
        return;
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    if (marked)
        m_markedRows.insert(seq);
    else
        m_markedRows.remove(seq);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, MarkedRole});
}

bool CanTraceModel::isMarked(int row) const
{
    if (row < 0 || row >= m_ringBuffer.size())
        return false;
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    return m_markedRows.contains(seq);
}

void CanTraceModel::clearMarks()
{
    m_markedRows.clear();
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole, MarkedRole});
}

QList<int> CanTraceModel::markedRows() const
{
    QList<int> result;
    int size = m_ringBuffer.size();
    for (int row = 0; row < size; ++row) {
        quint64 seq = m_seqCounter - size + row;
        if (m_markedRows.contains(seq))
            result.append(row);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void CanTraceModel::setRowColor(int row, const QColor &color)
{
    if (row < 0 || row >= m_ringBuffer.size())
        return;
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    if (color.isValid())
        m_rowColors[seq] = color;
    else
        m_rowColors.remove(seq);
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole});
}

QColor CanTraceModel::rowColor(int row) const
{
    if (row < 0 || row >= m_ringBuffer.size())
        return {};
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    return m_rowColors.value(seq);
}

void CanTraceModel::clearColors()
{
    m_rowColors.clear();
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole});
}

// ============================================================
//  行标签（Notepad++ 风格书签）
// ============================================================

void CanTraceModel::setRowLabel(int row, const QString &label)
{
    if (row < 0 || row >= m_ringBuffer.size())
        return;
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    if (label.isEmpty())
        m_rowLabels.remove(seq);
    else
        m_rowLabels[seq] = label;
    emit dataChanged(index(row, 0), index(row, ColCount - 1),
                     {Qt::BackgroundRole, Qt::ToolTipRole});
}

QString CanTraceModel::rowLabel(int row) const
{
    if (row < 0 || row >= m_ringBuffer.size())
        return {};
    quint64 seq = m_seqCounter - m_ringBuffer.size() + row;
    return m_rowLabels.value(seq);
}

QList<QPair<int, QString>> CanTraceModel::labeledMarks() const
{
    QList<QPair<int, QString>> result;
    int size = m_ringBuffer.size();
    for (int row = 0; row < size; ++row) {
        quint64 seq = m_seqCounter - size + row;
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
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
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

    for (const auto &r : rules) {
        if (!r.enabled)
            continue;
        auto *fe = new FilterEngine();
        if (fe->compile(r.expr))
            m_colorFilters.append(fe);
        else
            delete fe;
    }

    invalidateRowCache();
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole, Qt::ForegroundRole});
}

void CanTraceModel::clearColorRules()
{
    for (auto *fe : m_colorFilters)
        delete fe;
    m_colorFilters.clear();
    m_colorRules.clear();

    invalidateRowCache();
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole, Qt::ForegroundRole});
}

QColor CanTraceModel::evaluateColorRules(const CanFrame &frame) const
{
    for (int i = 0; i < m_colorFilters.size(); ++i) {
        if (m_colorFilters[i]->evaluate(frame))
            return m_colorRules[i].background;
    }
    return {};
}

// ============================================================
//  覆盖模式
// ============================================================

void CanTraceModel::setOverwriteMode(bool mode)
{
    if (m_overwriteMode == mode)
        return;
    m_overwriteMode = mode;
    if (mode) {
        // 进入覆盖模式：构建 ID→行映射
        m_idToRow.clear();
        for (int i = 0; i < m_ringBuffer.size(); ++i)
            m_idToRow[m_ringBuffer.at(i).id] = i;
    } else {
        m_idToRow.clear();
    }
}
