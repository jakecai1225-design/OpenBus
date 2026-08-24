#include "cantracemodel.h"
#include "core/filter_engine.h"
#include "core/dbcmanager.h"
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
        // G17: 支持用户自定义对齐
        return int(effectiveAlignment(index.column()));
    }

    if (role == Qt::ForegroundRole) {
        // 着色规则前景色优先（此前完全未生效）
        int ruleIdx = matchingColorRule(f);
        if (ruleIdx >= 0 && m_colorRules[ruleIdx].foreground.isValid())
            return m_colorRules[ruleIdx].foreground;
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
        int ruleIdx = matchingColorRule(f);
        if (ruleIdx >= 0)
            return m_colorRules[ruleIdx].background;
        if (m_markedRows.contains(seq))
            return QColor(0xFF, 0xF3, 0xB0);
        // 时间参考点行高亮
        if (m_hasTimeRef && seq == m_timeRefSeq)
            return QColor(0xB2, 0xDF, 0xDB);
        if (m_errorFrameHighlight && f.isErrorFrame())
            return QColor(0xFF, 0xCD, 0xD2);
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
    case ColName:       return QStringLiteral("Name");
    case ColDlc:        return QStringLiteral("DLC");
    case ColData:       return QStringLiteral("Data");
    case ColFlags:      return QStringLiteral("Flags");
    case ColFrameCount: return QStringLiteral("Count");
    case ColSignal:     return QStringLiteral("Signals");
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
        if (m_hasTimeRef)
            out = CanUtils::formatTime(f.timestamp - m_timeRefTimestamp);
        else
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
    case ColName: {
        // DBC 报文名称（未加载 DBC 或未匹配时为空）
        if (!m_dbcManager) {
            out = QStringLiteral("");
            return;
        }
        const DbcMessage *msg = m_dbcManager->findMessage(f.id);
        out = msg ? msg->name : QStringLiteral("");
        return;
    }
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
    case ColSignal: {
        if (!m_dbcManager) {
            out = QStringLiteral("");
            return;
        }
        // 查找匹配的报文定义
        const DbcMessage *msg = m_dbcManager->findMessage(f.id);
        if (!msg) {
            out = QStringLiteral("");
            return;
        }
        // 解码所有信号
        auto decoded = m_dbcManager->decodeFrame(f.id, f.data);
        if (decoded.isEmpty()) {
            out = QStringLiteral("");
            return;
        }
        // 格式化: Sig1=value Sig2=value ...
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
        // 截断过长内容
        if (out.length() > 200)
            out = out.left(200) + QStringLiteral("...");
        return;
    }
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
    // 首帧提交时记录 wall-clock 捕获起始时间
    if (m_seqCounter == 0)
        m_captureStartDateTime = QDateTime::currentDateTime();

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
    m_captureStartDateTime = QDateTime();
    m_idToRow.clear();
    m_idCount.clear();
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
    m_ringBuffer.reserve(max);
    m_seqCounter = 0;
    m_captureStartDateTime = QDateTime();
    m_idToRow.clear();
    m_idCount.clear();
    m_markedRows.clear();
    m_rowColors.clear();
    m_rowLabels.clear();
    m_hasTimeRef = false;
    m_timeRefSeq = 0;
    m_timeRefTimestamp = 0.0;
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
//  唯一值收集（Excel 风格筛选面板）
// ============================================================

QList<QPair<QString, int>> CanTraceModel::uniqueValues(int column) const
{
    QList<QPair<QString, int>> result;
    int total = m_ringBuffer.size();
    if (total == 0)
        return result;

    // ID 列：直接利用 m_idCount，效率最高
    if (column == ColId) {
        // 需要判断是否为扩展帧——遍历查找第一个匹配帧
        for (auto it = m_idCount.constBegin(); it != m_idCount.constEnd(); ++it) {
            bool extended = false;
            for (int i = 0; i < total; ++i) {
                if (m_ringBuffer.at(i).id == it.key()) {
                    extended = m_ringBuffer.at(i).extended;
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

    // 其他列：遍历所有帧，收集唯一值
    QHash<QString, int> valueCounts;
    QString tmp;
    for (int i = 0; i < total; ++i) {
        const CanFrame &f = m_ringBuffer.at(i);
        formatCell(i, column, f, tmp);
        valueCounts[tmp]++;
    }

    // 转换为列表并排序
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
//  时间参考点
// ============================================================

void CanTraceModel::setTimeReference(int row)
{
    if (row < 0 || row >= m_ringBuffer.size())
        return;
    quint64 oldSeq = m_timeRefSeq;
    m_timeRefSeq = m_seqCounter - m_ringBuffer.size() + row;
    m_timeRefTimestamp = m_ringBuffer.at(row).timestamp;
    m_hasTimeRef = true;
    invalidateRowCache();
    if (m_ringBuffer.size() > 0) {
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
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
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::DisplayRole, Qt::BackgroundRole});
}

int CanTraceModel::timeReferenceRow() const
{
    if (!m_hasTimeRef)
        return -1;
    int row = static_cast<int>(m_timeRefSeq) - (m_seqCounter - m_ringBuffer.size());
    if (row < 0 || row >= m_ringBuffer.size())
        return -1;
    return row;
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

int CanTraceModel::matchingColorRule(const CanFrame &frame) const
{
    for (int i = 0; i < m_colorFilters.size(); ++i) {
        if (m_colorFilters[i] && m_colorFilters[i]->evaluate(frame))
            return i;
    }
    return -1;
}

// ============================================================
//  DBC 管理器
// ============================================================

void CanTraceModel::setDbcManager(DbcManager *mgr)
{
    m_dbcManager = mgr;
    invalidateRowCache();
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::DisplayRole});
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

// ============================================================
//  错误帧高亮
// ============================================================

void CanTraceModel::setErrorFrameHighlight(bool enabled)
{
    if (m_errorFrameHighlight == enabled)
        return;
    m_errorFrameHighlight = enabled;
    invalidateRowCache();
    if (m_ringBuffer.size() > 0)
        emit dataChanged(index(0, 0), index(m_ringBuffer.size() - 1, ColCount - 1),
                         {Qt::BackgroundRole});
}

// ============================================================
//  列对齐配置
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
