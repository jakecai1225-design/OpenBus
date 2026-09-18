#include "cantraceproxymodel.h"
#include "cantracemodel.h"
#include "core/canframe.h"
#include "core/filter_engine.h"
#include "utils/canutils.h"

#include <algorithm>

CanTraceProxyModel::CanTraceProxyModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

bool CanTraceProxyModel::hasActiveFilters() const
{
    return filterActive() || !m_columnFilters.isEmpty() || !m_columnFilterValues.isEmpty();
}

bool CanTraceProxyModel::isAppendOnlyOrder() const
{
    return (m_sortColumn < 0)
        || (m_sortColumn == CanTraceModel::ColNo && m_sortOrder == Qt::AscendingOrder);
}

CanTraceProxyModel::MapMode CanTraceProxyModel::computeMapMode() const
{
    if (!isAppendOnlyOrder())
        return MapMode::DenseMaps;
    if (!hasActiveFilters())
        return MapMode::Passthrough;
    return MapMode::AcceptIndex;
}

int CanTraceProxyModel::proxyRowCount() const
{
    if (m_mapMode == MapMode::Passthrough) {
        auto *src = sourceModel();
        return src ? src->rowCount() : 0;
    }
    return m_acceptIndex.size();
}

int CanTraceProxyModel::displayedCount() const
{
    return proxyRowCount();
}

int CanTraceProxyModel::sourceRowAtProxy(int proxyRow) const
{
    if (m_mapMode == MapMode::Passthrough)
        return proxyRow;
    if (proxyRow < 0 || proxyRow >= m_acceptIndex.size())
        return -1;
    return m_acceptIndex.at(proxyRow);
}

int CanTraceProxyModel::proxyRowOfSource(int sourceRow) const
{
    if (sourceRow < 0)
        return -1;
    if (m_mapMode == MapMode::Passthrough) {
        auto *src = sourceModel();
        if (!src || sourceRow >= src->rowCount())
            return -1;
        return sourceRow;
    }
    if (m_mapMode == MapMode::DenseMaps)
        return m_sourceToProxy.value(sourceRow, -1);
    return m_acceptIndex.indexOf(sourceRow);
}

// ============================================================
//  QAbstractProxyModel mapping
// ============================================================

void CanTraceProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (sourceModel == this->sourceModel())
        return;

    beginResetModel();

    if (this->sourceModel())
        disconnect(this->sourceModel(), nullptr, this, nullptr);

    QAbstractProxyModel::setSourceModel(sourceModel);

    if (sourceModel)
        disconnect(sourceModel, nullptr, this, nullptr);

    if (sourceModel) {
        connect(sourceModel, &QAbstractItemModel::rowsInserted,
                this, &CanTraceProxyModel::onSourceRowsInserted);
        connect(sourceModel, &QAbstractItemModel::rowsRemoved,
                this, &CanTraceProxyModel::onSourceRowsRemoved);
        connect(sourceModel, &QAbstractItemModel::dataChanged,
                this, &CanTraceProxyModel::onSourceDataChanged);
        if (auto *tm = qobject_cast<CanTraceModel *>(sourceModel)) {
            connect(tm, &CanTraceModel::ringWrapped,
                    this, &CanTraceProxyModel::onSourceRingWrapped);
        }
        connect(sourceModel, &QAbstractItemModel::modelAboutToBeReset,
                this, &CanTraceProxyModel::onSourceModelAboutToBeReset);
        connect(sourceModel, &QAbstractItemModel::modelReset,
                this, &CanTraceProxyModel::onSourceModelReset);
        connect(sourceModel, &QAbstractItemModel::headerDataChanged,
                this, &QAbstractItemModel::headerDataChanged);
    }

    buildMapping();
    if (auto *m = traceModel())
        m_lastSeq = m->seqCounter();
    endResetModel();
    emitPacketCount();
}

QModelIndex CanTraceProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || proxyIndex.model() != this)
        return {};
    const int srcRow = sourceRowAtProxy(proxyIndex.row());
    auto *src = sourceModel();
    if (!src || srcRow < 0 || srcRow >= src->rowCount())
        return {};
    return src->index(srcRow, proxyIndex.column());
}

QModelIndex CanTraceProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.model() != sourceModel())
        return {};
    const int p = proxyRowOfSource(sourceIndex.row());
    if (p < 0 || p >= proxyRowCount())
        return {};
    return createIndex(p, sourceIndex.column());
}

int CanTraceProxyModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return proxyRowCount();
}

int CanTraceProxyModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    auto *src = sourceModel();
    return src ? src->columnCount() : 0;
}

QModelIndex CanTraceProxyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid())
        return {};
    if (row < 0 || row >= proxyRowCount() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex CanTraceProxyModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child);
    return {};
}

QVariant CanTraceProxyModel::data(const QModelIndex &proxyIndex, int role) const
{
    if (role == Qt::DisplayRole && proxyIndex.isValid()
        && proxyIndex.column() == CanTraceModel::ColTime) {
        auto *model = traceModel();
        if (!model)
            return {};
        QModelIndex sourceIdx = mapToSource(proxyIndex);
        if (!sourceIdx.isValid())
            return {};
        const CanFrame f = model->frameAt(sourceIdx.row());
        int prec = m_timePrecision;

        switch (m_timestampMode) {
        case Absolute:
            return CanUtils::formatTime(f.timestamp, prec);
        case SinceCapture: {
            double prev = (sourceIdx.row() > 0)
                ? model->frameAt(sourceIdx.row() - 1).timestamp : 0.0;
            return CanUtils::formatTime(f.timestamp - prev, prec);
        }
        case SinceDisplay:
            return CanUtils::formatTime(deltaSortFrozen() ? deltaKey(sourceIdx.row())
                                                          : displayDelta(sourceIdx.row()), prec);
        case DateTimeOfDay:
            return CanUtils::formatDateTime(model->captureStartTime(), f.timestamp, prec);
        case SecondsSinceEpoch: {
            QDateTime start = model->captureStartTime();
            if (start.isValid())
                return CanUtils::formatTime(start.toMSecsSinceEpoch() / 1000.0 + f.timestamp, prec);
            return CanUtils::formatTime(f.timestamp, prec);
        }
        }
        return {};
    }
    return QAbstractProxyModel::data(proxyIndex, role);
}

// ============================================================
//  Main filter expression
// ============================================================

bool CanTraceProxyModel::setFilterExpression(const QString &expr)
{
    m_expr = expr;
    if (expr.trimmed().isEmpty()) {
        m_filterEngine.reset();
    } else {
        auto engine = std::make_unique<FilterEngine>();
        if (!engine->compile(expr)) {
            m_filterEngine.reset();
            rebuildMapping();
            emitPacketCount();
            return false;
        }
        m_filterEngine = std::move(engine);
    }
    rebuildMapping();
    emitPacketCount();
    return true;
}

void CanTraceProxyModel::clearFilter()
{
    m_expr.clear();
    m_filterEngine.reset();
    rebuildMapping();
    emitPacketCount();
}

// ============================================================
//  Column filters
// ============================================================

void CanTraceProxyModel::setColumnFilter(int column, const QString &text)
{
    if (text.trimmed().isEmpty())
        m_columnFilters.remove(column);
    else
        m_columnFilters[column] = text.trimmed();
    rebuildMapping();
    emitPacketCount();
}

void CanTraceProxyModel::clearColumnFilter(int column)
{
    m_columnFilters.remove(column);
    rebuildMapping();
    emitPacketCount();
}

void CanTraceProxyModel::clearAllColumnFilters()
{
    m_columnFilters.clear();
    m_columnFilterValues.clear();
    rebuildMapping();
    emitPacketCount();
}

bool CanTraceProxyModel::hasColumnFilter(int column) const
{
    return m_columnFilters.contains(column) || m_columnFilterValues.contains(column);
}

bool CanTraceProxyModel::matchColumnFilter(int sourceRow, int column) const
{
    auto *model = traceModel();
    if (!model) return true;

    const CanFrame frame = model->frameAt(sourceRow);
    QString filter = m_columnFilters.value(column).toLower();

    switch (column) {
    case CanTraceModel::ColNo: {
        qint64 seq = static_cast<qint64>(model->seqCounter() - (quint64)model->rowCount()
                                         + (quint64)sourceRow + 1);
        if (filter.startsWith(">"))
            return seq > filter.mid(1).trimmed().toLongLong();
        if (filter.startsWith("<"))
            return seq < filter.mid(1).trimmed().toLongLong();
        return QString::number(seq).contains(filter);
    }
    case CanTraceModel::ColTime: {
        QString val = CanUtils::formatTime(frame.timestamp);
        if (filter.startsWith(">"))
            return frame.timestamp > filter.mid(1).trimmed().toDouble();
        if (filter.startsWith("<"))
            return frame.timestamp < filter.mid(1).trimmed().toDouble();
        return val.contains(filter);
    }
    case CanTraceModel::ColDelta: {
        double prev = (sourceRow > 0) ? model->frameAt(sourceRow - 1).timestamp : frame.timestamp;
        double delta = frame.timestamp - prev;
        QString val = CanUtils::formatTime(delta);
        if (filter.startsWith(">"))
            return delta > filter.mid(1).trimmed().toDouble();
        if (filter.startsWith("<"))
            return delta < filter.mid(1).trimmed().toDouble();
        return val.contains(filter);
    }
    case CanTraceModel::ColChannel:
        return QString::number(frame.channel).contains(filter);
    case CanTraceModel::ColDirection: {
        QString dir = (frame.direction == CanFrame::Rx) ? QStringLiteral("rx") : QStringLiteral("tx");
        return dir.contains(filter);
    }
    case CanTraceModel::ColId: {
        if (filter.startsWith(">")) {
            quint32 cmp = CanUtils::parseHex(filter.mid(1).trimmed());
            return frame.id > cmp;
        }
        if (filter.startsWith("<")) {
            quint32 cmp = CanUtils::parseHex(filter.mid(1).trimmed());
            return frame.id < cmp;
        }
        if (filter.startsWith("!=")) {
            quint32 cmp = CanUtils::parseHex(filter.mid(2).trimmed());
            return frame.id != cmp;
        }
        QString idStr = CanUtils::formatId(frame.id, frame.extended).toLower();
        return idStr.contains(filter);
    }
    case CanTraceModel::ColName:
        return messageName(sourceRow).toLower().contains(filter);
    case CanTraceModel::ColDlc:
        return CanUtils::formatDlc(frame.dlc, frame.fd).contains(filter);
    case CanTraceModel::ColData:
        return CanUtils::formatData(frame.data).toLower().contains(filter);
    case CanTraceModel::ColFlags:
        return CanUtils::formatFlags(frame).toLower().contains(filter);
    case CanTraceModel::ColFrameCount: {
        int count = model->frameCountForKey(frame.id, frame.channel);
        if (filter.startsWith(">"))
            return count > filter.mid(1).trimmed().toInt();
        if (filter.startsWith("<"))
            return count < filter.mid(1).trimmed().toInt();
        return QString::number(count).contains(filter);
    }
    case CanTraceModel::ColInterval:
        return true;  // numeric interval — value-set / display filter via formatted cell
    }
    return true;
}

// ============================================================
//  Value-set filters (Excel-style)
// ============================================================

void CanTraceProxyModel::setColumnFilterValues(int column, const QSet<QString> &values)
{
    if (values.isEmpty())
        m_columnFilterValues.remove(column);
    else
        m_columnFilterValues[column] = values;
    rebuildMapping();
    emitPacketCount();
}

void CanTraceProxyModel::clearColumnFilterValues(int column)
{
    m_columnFilterValues.remove(column);
    rebuildMapping();
    emitPacketCount();
}

QString CanTraceProxyModel::columnDisplayText(int sourceRow, int column) const
{
    auto *model = traceModel();
    if (!model)
        return {};
    QModelIndex idx = model->index(sourceRow, column);
    return model->data(idx, Qt::DisplayRole).toString();
}

bool CanTraceProxyModel::matchColumnFilterValues(int sourceRow, int column) const
{
    auto it = m_columnFilterValues.constFind(column);
    if (it == m_columnFilterValues.end())
        return true;

    QString displayText = columnDisplayText(sourceRow, column);
    return it.value().contains(displayText);
}

bool CanTraceProxyModel::filterAcceptsRow(int sourceRow) const
{
    if (m_filterEngine && m_filterEngine->isValid() && !m_filterEngine->isEmpty()) {
        auto *model = traceModel();
        if (!model) return true;
        const CanFrame frame = model->frameAt(sourceRow);
        if (!m_filterEngine->evaluate(frame))
            return false;
    }

    for (auto it = m_columnFilters.constBegin(); it != m_columnFilters.constEnd(); ++it) {
        if (!matchColumnFilter(sourceRow, it.key()))
            return false;
    }

    for (auto it = m_columnFilterValues.constBegin(); it != m_columnFilterValues.constEnd(); ++it) {
        if (!matchColumnFilterValues(sourceRow, it.key()))
            return false;
    }

    return true;
}

// ============================================================
//  Timestamp display mode
// ============================================================

void CanTraceProxyModel::setTimestampMode(TimestampMode mode)
{
    if (m_timestampMode == mode)
        return;
    m_timestampMode = mode;

    if (m_sortColumn == CanTraceModel::ColTime) {
        if (mode == SinceDisplay) {
            refreshDeltaKeys();
            m_deltaSortFrozen = true;
        } else {
            m_deltaSortFrozen = false;
        }
        resortCurrent();
    }

    int rows = proxyRowCount();
    if (rows > 0) {
        emit dataChanged(index(0, CanTraceModel::ColTime),
                         index(rows - 1, CanTraceModel::ColTime),
                         {Qt::DisplayRole});
    }
}

void CanTraceProxyModel::setTimePrecision(int precision)
{
    if (m_timePrecision == precision)
        return;
    m_timePrecision = precision;

    int rows = proxyRowCount();
    if (rows > 0) {
        emit dataChanged(index(0, CanTraceModel::ColTime),
                         index(rows - 1, CanTraceModel::ColTime),
                         {Qt::DisplayRole});
        emit dataChanged(index(0, CanTraceModel::ColDelta),
                         index(rows - 1, CanTraceModel::ColDelta),
                         {Qt::DisplayRole});
    }
}

int CanTraceProxyModel::capturedCount() const
{
    auto *model = traceModel();
    return model ? model->frameCount() : 0;
}

void CanTraceProxyModel::emitPacketCount()
{
    emit packetCountChanged(capturedCount(), displayedCount());
}

// ============================================================
//  Sort
// ============================================================

void CanTraceProxyModel::sort(int column, Qt::SortOrder order)
{
    if (column == m_sortColumn && order == m_sortOrder)
        return;

    m_sortColumn = column;
    m_sortOrder = order;

    if (column < 0) {
        m_deltaSortFrozen = false;
        rebuildMapping();
        return;
    }

    if (column == CanTraceModel::ColTime && m_timestampMode == SinceDisplay) {
        if (!m_deltaSortFrozen) {
            refreshDeltaKeys();
            m_deltaSortFrozen = true;
        }
    } else {
        m_deltaSortFrozen = false;
    }

    rebuildMapping();
}

void CanTraceProxyModel::resortCurrent()
{
    withLayoutChange([this]() {
        auto &rows = m_acceptIndex.rowsMutable();
        std::stable_sort(rows.begin(), rows.end(),
                         [this](quint32 a, quint32 b) {
                             return m_sortOrder == Qt::AscendingOrder
                                 ? lessThan(static_cast<int>(a), static_cast<int>(b))
                                 : lessThan(static_cast<int>(b), static_cast<int>(a));
                         });
        m_mapMode = MapMode::DenseMaps;
        rebuildSourceToProxy();
    });
}

bool CanTraceProxyModel::lessThan(int sourceLeft, int sourceRight) const
{
    auto *model = traceModel();
    if (!model)
        return sourceLeft < sourceRight;

    const CanFrame fl = model->frameAt(sourceLeft);
    const CanFrame fr = model->frameAt(sourceRight);

    switch (m_sortColumn) {
    case CanTraceModel::ColNo:
        return sourceLeft < sourceRight;
    case CanTraceModel::ColTime:
        if (m_deltaSortFrozen)
            return deltaKey(sourceLeft) < deltaKey(sourceRight);
        return fl.timestamp < fr.timestamp;
    case CanTraceModel::ColDelta: {
        double dl = (sourceLeft > 0) ? fl.timestamp - model->frameAt(sourceLeft - 1).timestamp : 0.0;
        double dr = (sourceRight > 0) ? fr.timestamp - model->frameAt(sourceRight - 1).timestamp : 0.0;
        return dl < dr;
    }
    case CanTraceModel::ColChannel:
        return fl.channel < fr.channel;
    case CanTraceModel::ColDirection:
        return fl.direction < fr.direction;
    case CanTraceModel::ColId:
        return fl.id < fr.id;
    case CanTraceModel::ColName:
        return messageName(sourceLeft).toLower() < messageName(sourceRight).toLower();
    case CanTraceModel::ColDlc:
        return fl.dlc < fr.dlc;
    case CanTraceModel::ColData:
        return fl.data < fr.data;
    case CanTraceModel::ColFlags:
        return CanUtils::formatFlags(fl) < CanUtils::formatFlags(fr);
    case CanTraceModel::ColFrameCount:
        return model->frameCountForKey(fl.id, fl.channel)
               < model->frameCountForKey(fr.id, fr.channel);
    case CanTraceModel::ColInterval:
        return columnDisplayText(sourceLeft, CanTraceModel::ColInterval)
               < columnDisplayText(sourceRight, CanTraceModel::ColInterval);
    }

    return sourceLeft < sourceRight;
}

// ============================================================
//  Mapping rebuild (T2 lean modes)
// ============================================================

CanTraceModel *CanTraceProxyModel::traceModel() const
{
    return qobject_cast<CanTraceModel *>(sourceModel());
}

QString CanTraceProxyModel::messageName(int sourceRow) const
{
    return columnDisplayText(sourceRow, CanTraceModel::ColName);
}

double CanTraceProxyModel::displayDelta(int sourceRow) const
{
    auto *model = traceModel();
    if (!model || sourceRow < 0 || sourceRow >= model->rowCount())
        return 0.0;
    int p = proxyRowOfSource(sourceRow);
    double ts = model->frameAt(sourceRow).timestamp;
    if (p <= 0)
        return ts;
    const int prevSrc = sourceRowAtProxy(p - 1);
    if (prevSrc < 0)
        return ts;
    return ts - model->frameAt(prevSrc).timestamp;
}

void CanTraceProxyModel::refreshDeltaKeys()
{
    auto *model = traceModel();
    int n = model ? model->rowCount() : 0;
    m_displayDeltaKeys.resize(n);
    m_displayDeltaKeys.fill(0.0);
    const int proxyN = proxyRowCount();
    if (!model || proxyN <= 0) {
        m_lastAcceptedSourceRow = -1;
        return;
    }
    double prevTs = 0.0;
    int maxSrc = -1;
    for (int p = 0; p < proxyN; ++p) {
        const int src = sourceRowAtProxy(p);
        if (src < 0)
            continue;
        const double ts = model->frameAt(src).timestamp;
        m_displayDeltaKeys[src] = (p == 0) ? ts : ts - prevTs;
        prevTs = ts;
        if (src > maxSrc)
            maxSrc = src;
    }
    m_lastAcceptedSourceRow = maxSrc;
}

double CanTraceProxyModel::deltaKey(int sourceRow) const
{
    return (sourceRow >= 0 && sourceRow < m_displayDeltaKeys.size())
               ? m_displayDeltaKeys.at(sourceRow) : 0.0;
}

void CanTraceProxyModel::buildMapping()
{
    auto *model = traceModel();
    m_acceptIndex.clear();
    m_sourceToProxy.clear();
    if (!model) {
        m_mapMode = MapMode::Passthrough;
        return;
    }

    m_mapMode = computeMapMode();
    const int total = model->rowCount();

    if (m_mapMode == MapMode::Passthrough) {
        // Identity: no accepted list, no reverse map.
        if (m_deltaSortFrozen)
            refreshDeltaKeys();
        return;
    }

    m_acceptIndex.reserve(hasActiveFilters() ? qMin(total, 4096) : total);
    for (int i = 0; i < total; ++i) {
        if (!filterAcceptsRow(i))
            continue;
        m_acceptIndex.append(i);
    }

    if (m_deltaSortFrozen)
        refreshDeltaKeys();

    if (m_mapMode == MapMode::DenseMaps) {
        auto &rows = m_acceptIndex.rowsMutable();
        std::stable_sort(rows.begin(), rows.end(),
                         [this](quint32 a, quint32 b) {
                             return m_sortOrder == Qt::AscendingOrder
                                 ? lessThan(static_cast<int>(a), static_cast<int>(b))
                                 : lessThan(static_cast<int>(b), static_cast<int>(a));
                         });
        rebuildSourceToProxy();
    }
}

void CanTraceProxyModel::rebuildSourceToProxy()
{
    auto *model = traceModel();
    const int total = model ? model->rowCount() : 0;
    m_sourceToProxy.resize(total);
    std::fill(m_sourceToProxy.begin(), m_sourceToProxy.end(), -1);
    for (int p = 0; p < m_acceptIndex.size(); ++p)
        m_sourceToProxy[m_acceptIndex.at(p)] = p;
}

void CanTraceProxyModel::rebuildMapping()
{
    withLayoutChange([this]() { buildMapping(); });
}

void CanTraceProxyModel::withLayoutChange(const std::function<void()> &mutate)
{
    const QModelIndexList persist = persistentIndexList();
    QVector<int> srcRows;
    srcRows.reserve(persist.size());
    for (const QModelIndex &p : persist)
        srcRows.append(sourceRowAtProxy(p.row()));

    emit layoutAboutToBeChanged();
    mutate();

    QModelIndexList from, to;
    for (int i = 0; i < persist.size(); ++i) {
        if (!persist.at(i).isValid())
            continue;
        int p = srcRows.at(i) >= 0 ? proxyRowOfSource(srcRows.at(i)) : -1;
        from.append(persist.at(i));
        to.append(p >= 0 ? createIndex(p, persist.at(i).column()) : QModelIndex());
    }
    if (!from.isEmpty())
        changePersistentIndexList(from, to);
    emit layoutChanged();
}

void CanTraceProxyModel::forwardDataChanged(int srcTop, int srcBottom, const QVector<int> &roles)
{
    const int n = proxyRowCount();
    if (n <= 0 || srcBottom < srcTop || srcTop < 0)
        return;

    int pTop = -1, pBottom = -1;
    if (m_mapMode == MapMode::Passthrough) {
        pTop = srcTop;
        pBottom = qMin(srcBottom, n - 1);
    } else if (!hasActiveFilters() && m_mapMode == MapMode::DenseMaps
               && m_acceptIndex.size() == m_sourceToProxy.size()) {
        pTop = 0;
        pBottom = n - 1;
    } else {
        for (int r = srcTop; r <= srcBottom; ++r) {
            int p = proxyRowOfSource(r);
            if (p >= 0) {
                if (pTop < 0)
                    pTop = p;
                pBottom = p;
            }
        }
    }
    if (pTop < 0)
        return;
    emit dataChanged(index(pTop, 0), index(pBottom, columnCount() - 1), roles);
}

// ============================================================
//  Source model signals
// ============================================================

void CanTraceProxyModel::onSourceRowsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(parent);
    auto *model = traceModel();
    if (!model)
        return;

    m_lastSeq = model->seqCounter();

    if (m_mapMode == MapMode::Passthrough) {
        beginInsertRows({}, first, last);
        endInsertRows();
        emitPacketCount();
        return;
    }

    // Only tail inserts are incremental (CanTraceModel / CaptureLog camera).
    if (last != model->rowCount() - 1) {
        rebuildMapping();
        emitPacketCount();
        return;
    }

    const int newCount = last - first + 1;
    if (m_mapMode == MapMode::DenseMaps && first != m_sourceToProxy.size()) {
        rebuildMapping();
        emitPacketCount();
        return;
    }
    if (m_mapMode == MapMode::DenseMaps)
        m_sourceToProxy.resize(m_sourceToProxy.size() + newCount, -1);

    QVector<int> accepted;
    accepted.reserve(newCount);
    const bool deltaFrozen = m_deltaSortFrozen;
    if (deltaFrozen)
        m_displayDeltaKeys.resize(model->rowCount());
    for (int r = first; r <= last; ++r) {
        if (!filterAcceptsRow(r))
            continue;
        accepted.append(r);
        if (deltaFrozen) {
            const double prevTs = (m_lastAcceptedSourceRow >= 0)
                ? model->frameAt(m_lastAcceptedSourceRow).timestamp : 0.0;
            m_displayDeltaKeys[r] = model->frameAt(r).timestamp - prevTs;
            m_lastAcceptedSourceRow = r;
        }
    }

    if (m_mapMode == MapMode::AcceptIndex) {
        if (!accepted.isEmpty()) {
            const int insertStart = m_acceptIndex.size();
            beginInsertRows({}, insertStart, insertStart + accepted.size() - 1);
            m_acceptIndex.appendMany(accepted);
            endInsertRows();
        }
        emitPacketCount();
        return;
    }

    // DenseMaps: merge accepted rows by sort key.
    if (!accepted.isEmpty()) {
        std::stable_sort(accepted.begin(), accepted.end(),
                         [this](int a, int b) {
                             return m_sortOrder == Qt::AscendingOrder
                                 ? lessThan(a, b) : lessThan(b, a);
                         });
        withLayoutChange([this, &accepted]() {
            QVector<quint32> merged;
            merged.resize(m_acceptIndex.size() + accepted.size());
            QVector<quint32> incoming;
            incoming.reserve(accepted.size());
            for (int r : accepted)
                incoming.append(static_cast<quint32>(r));
            std::merge(m_acceptIndex.rows().begin(), m_acceptIndex.rows().end(),
                       incoming.begin(), incoming.end(), merged.begin(),
                       [this](quint32 a, quint32 b) {
                           return m_sortOrder == Qt::AscendingOrder
                               ? lessThan(static_cast<int>(a), static_cast<int>(b))
                               : lessThan(static_cast<int>(b), static_cast<int>(a));
                       });
            m_acceptIndex.rowsMutable() = std::move(merged);
            rebuildSourceToProxy();
        });
    }
    emitPacketCount();
}

void CanTraceProxyModel::onSourceRowsRemoved(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);
    rebuildMapping();
    if (auto *m = traceModel())
        m_lastSeq = m->seqCounter();
    emitPacketCount();
}

void CanTraceProxyModel::onSourceDataChanged(const QModelIndex &topLeft,
                                             const QModelIndex &bottomRight,
                                             const QVector<int> &roles)
{
    auto *model = traceModel();
    if (!model)
        return;

    int srcRows = model->rowCount();
    int srcTop = qMax(0, topLeft.row());
    int srcBottom = qMin(srcRows - 1, bottomRight.row());
    if (srcBottom < srcTop)
        return;

    quint64 seq = model->seqCounter();

    if (srcTop == 0 && srcBottom == srcRows - 1 && srcRows > 0
        && seq != m_lastSeq
        && (m_mapMode == MapMode::Passthrough
            || (m_mapMode == MapMode::DenseMaps && srcRows == m_sourceToProxy.size())
            || m_mapMode == MapMode::AcceptIndex)) {
        int shift = static_cast<int>(qMin<quint64>(seq - m_lastSeq, static_cast<quint64>(srcRows)));
        m_lastSeq = seq;
        handleFullShift(shift, roles);
        return;
    }
    m_lastSeq = seq;

    int span = srcBottom - srcTop + 1;
    bool mayAffectFilter = hasActiveFilters()
        && span <= 64
        && (roles.isEmpty() || roles.contains(Qt::DisplayRole));
    if (mayAffectFilter) {
        bool changed = false;
        for (int r = srcTop; r <= srcBottom; ++r) {
            bool acceptedNow = filterAcceptsRow(r);
            bool acceptedBefore = proxyRowOfSource(r) >= 0;
            if (acceptedNow != acceptedBefore) {
                changed = true;
                break;
            }
        }
        if (changed) {
            rebuildMapping();
            emitPacketCount();
            return;
        }
    }

    forwardDataChanged(srcTop, srcBottom, roles);
}

void CanTraceProxyModel::onSourceModelAboutToBeReset()
{
    beginResetModel();
}

void CanTraceProxyModel::onSourceModelReset()
{
    if (auto *m = traceModel())
        m_lastSeq = m->seqCounter();
    buildMapping();
    endResetModel();
    emitPacketCount();
}

void CanTraceProxyModel::onSourceRingWrapped(int shift)
{
    auto *model = traceModel();
    if (!model)
        return;
    handleFullShift(shift, {Qt::DisplayRole, Qt::BackgroundRole, Qt::ForegroundRole,
                            CanTraceModel::MarkedRole});
    m_lastSeq = model->seqCounter();
}

void CanTraceProxyModel::handleFullShift(int shift, const QVector<int> &roles)
{
    auto *model = traceModel();
    if (!model)
        return;
    int n = model->rowCount();
    if (shift <= 0 || shift >= n) {
        rebuildMapping();
        forwardDataChanged(0, n - 1, roles);
        emitPacketCount();
        return;
    }

    if (m_mapMode == MapMode::Passthrough) {
        forwardDataChanged(0, n - 1, roles);
        emitPacketCount();
        return;
    }

    if (m_mapMode == MapMode::DenseMaps) {
        bool changed = false;
        for (int r = n - shift; r < n; ++r) {
            bool acceptedNow = filterAcceptsRow(r);
            bool acceptedBefore = m_sourceToProxy.value(r, -1) >= 0;
            if (acceptedNow != acceptedBefore) {
                changed = true;
                break;
            }
        }
        if (changed) {
            rebuildMapping();
        } else if (m_deltaSortFrozen) {
            m_displayDeltaKeys.resize(n);
            for (int s = 0; s + shift < n; ++s)
                m_displayDeltaKeys[s] = m_displayDeltaKeys[s + shift];
            m_lastAcceptedSourceRow = (m_lastAcceptedSourceRow >= shift)
                                          ? m_lastAcceptedSourceRow - shift : -1;
            for (int r = n - shift; r < n; ++r) {
                if (m_sourceToProxy.value(r, -1) < 0)
                    continue;
                const double prevTs = (m_lastAcceptedSourceRow >= 0)
                    ? model->frameAt(m_lastAcceptedSourceRow).timestamp : 0.0;
                m_displayDeltaKeys[r] = model->frameAt(r).timestamp - prevTs;
                m_lastAcceptedSourceRow = r;
            }
        }
        // Sorted views: source identities moved — rebuild maps.
        if (!changed)
            rebuildMapping();
        forwardDataChanged(0, n - 1, roles);
        emitPacketCount();
        return;
    }

    // AcceptIndex: shift accepted rows, evaluate new tail.
    const int oldCount = m_acceptIndex.size();
    QVector<int> newTail;
    newTail.reserve(shift);
    const int tailStart = n - shift;
    for (int r = tailStart; r < n; ++r) {
        if (filterAcceptsRow(r))
            newTail.append(r);
    }

    QVector<quint32> next;
    next.reserve(oldCount);
    for (quint32 s : m_acceptIndex.rows()) {
        if (static_cast<int>(s) >= shift)
            next.append(s - static_cast<quint32>(shift));
    }
    for (int r : newTail)
        next.append(static_cast<quint32>(r));
    const int newCount = next.size();

    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        m_acceptIndex.rowsMutable() = std::move(next);
        endInsertRows();
    } else if (newCount < oldCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        m_acceptIndex.rowsMutable() = std::move(next);
        endRemoveRows();
    } else {
        m_acceptIndex.rowsMutable() = std::move(next);
    }

    if (newCount > 0)
        emit dataChanged(index(0, 0), index(newCount - 1, columnCount() - 1), roles);
    emitPacketCount();
}
