#include "canfilterproxymodel.h"
#include "cantracemodel.h"
#include "core/canframe.h"
#include "core/filter_engine.h"
#include "utils/canutils.h"

CanFilterProxyModel::CanFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

bool CanFilterProxyModel::setFilterExpression(const QString &expr)
{
    m_expr = expr;
    if (expr.trimmed().isEmpty()) {
        m_filterEngine.reset();
        invalidateFilter();
        return true;
    }
    auto engine = std::make_unique<FilterEngine>();
    if (!engine->compile(expr)) {
        m_filterEngine.reset();
        invalidateFilter();
        return false;
    }
    m_filterEngine = std::move(engine);
    invalidateFilter();
    return true;
}

void CanFilterProxyModel::clearFilter()
{
    m_expr.clear();
    m_filterEngine.reset();
    invalidateFilter();
}

// ============================================================
//  按列过滤
// ============================================================

void CanFilterProxyModel::setColumnFilter(int column, const QString &text)
{
    if (text.trimmed().isEmpty()) {
        m_columnFilters.remove(column);
    } else {
        m_columnFilters[column] = text.trimmed();
    }
    invalidateFilter();
}

void CanFilterProxyModel::clearColumnFilter(int column)
{
    m_columnFilters.remove(column);
    invalidateFilter();
}

void CanFilterProxyModel::clearAllColumnFilters()
{
    m_columnFilters.clear();
    m_columnFilterValues.clear();
    invalidateFilter();
}

bool CanFilterProxyModel::hasColumnFilter(int column) const
{
    return m_columnFilters.contains(column) || m_columnFilterValues.contains(column);
}

bool CanFilterProxyModel::matchColumnFilter(int sourceRow, int column) const
{
    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    if (!model) return true;

    const CanFrame &frame = model->frameAt(sourceRow);
    QString filter = m_columnFilters.value(column).toLower();

    switch (column) {
    case CanTraceModel::ColNo: {
        // 支持 ">10", "<50", "123" 等
        int seq = sourceRow + 1;
        if (filter.startsWith(">"))
            return seq > filter.mid(1).trimmed().toInt();
        if (filter.startsWith("<"))
            return seq < filter.mid(1).trimmed().toInt();
        return QString::number(seq).contains(filter);
    }
    case CanTraceModel::ColTime: {
        // 支持 ">0.5", "<1.0", "0.3~0.8" 等范围
        QString val = CanUtils::formatTime(frame.timestamp);
        if (filter.startsWith(">"))
            return frame.timestamp > filter.mid(1).trimmed().toDouble();
        if (filter.startsWith("<"))
            return frame.timestamp < filter.mid(1).trimmed().toDouble();
        return val.contains(filter);
    }
    case CanTraceModel::ColDelta: {
        // 计算与上一帧的时间增量
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
        QString dir = (frame.direction == CanFrame::Rx) ? "rx" : "tx";
        return dir.contains(filter);
    }
    case CanTraceModel::ColId: {
        // 支持 "0x123", "123", ">0x100" 等
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
    case CanTraceModel::ColDlc:
        return CanUtils::formatDlc(frame.dlc, frame.fd).contains(filter);
    case CanTraceModel::ColData:
        return CanUtils::formatData(frame.data).toLower().contains(filter);
    case CanTraceModel::ColFlags:
        return CanUtils::formatFlags(frame).toLower().contains(filter);
    case CanTraceModel::ColFrameCount: {
        auto *traceModel = qobject_cast<CanTraceModel *>(sourceModel());
        int count = traceModel ? traceModel->frameCountForId(frame.id) : 0;
        if (filter.startsWith(">"))
            return count > filter.mid(1).trimmed().toInt();
        if (filter.startsWith("<"))
            return count < filter.mid(1).trimmed().toInt();
        return QString::number(count).contains(filter);
    }
    }
    return true;
}

// ============================================================
//  值集过滤（Excel 风格复选框）
// ============================================================

void CanFilterProxyModel::setColumnFilterValues(int column, const QSet<QString> &values)
{
    if (values.isEmpty()) {
        m_columnFilterValues.remove(column);
    } else {
        m_columnFilterValues[column] = values;
    }
    invalidateFilter();
}

void CanFilterProxyModel::clearColumnFilterValues(int column)
{
    m_columnFilterValues.remove(column);
    invalidateFilter();
}

QString CanFilterProxyModel::columnDisplayText(int sourceRow, int column) const
{
    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    if (!model)
        return {};
    // 获取源模型的显示文本（已格式化的文本）
    QModelIndex idx = model->index(sourceRow, column);
    return model->data(idx, Qt::DisplayRole).toString();
}

bool CanFilterProxyModel::matchColumnFilterValues(int sourceRow, int column) const
{
    auto it = m_columnFilterValues.find(column);
    if (it == m_columnFilterValues.end())
        return true;  // 该列无值集过滤

    QString displayText = columnDisplayText(sourceRow, column);
    return it.value().contains(displayText);
}

// ============================================================
//  行过滤（主表达式 + 列过滤）
// ============================================================

bool CanFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    Q_UNUSED(sourceParent);

    // 主过滤表达式
    if (m_filterEngine && m_filterEngine->isValid() && !m_filterEngine->isEmpty()) {
        auto *model = qobject_cast<CanTraceModel *>(sourceModel());
        if (!model) return true;
        const CanFrame &frame = model->frameAt(sourceRow);
        if (!m_filterEngine->evaluate(frame))
            return false;
    }

    // 按列过滤
    for (auto it = m_columnFilters.constBegin(); it != m_columnFilters.constEnd(); ++it) {
        if (!matchColumnFilter(sourceRow, it.key()))
            return false;
    }

    // 值集过滤（Excel 风格复选框）
    for (auto it = m_columnFilterValues.constBegin(); it != m_columnFilterValues.constEnd(); ++it) {
        if (!matchColumnFilterValues(sourceRow, it.key()))
            return false;
    }

    return true;
}

// ============================================================
//  排序 — 按列正确比较
// ============================================================

bool CanFilterProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    if (!model)
        return QSortFilterProxyModel::lessThan(left, right);

    const CanFrame &fl = model->frameAt(left.row());
    const CanFrame &fr = model->frameAt(right.row());

    switch (left.column()) {
    case CanTraceModel::ColNo:
        return left.row() < right.row();
    case CanTraceModel::ColTime:
        // 排序键须与 data() 显示值一致
        switch (m_timestampMode) {
        case SinceCapture: {
            double prevL = (left.row() > 0) ? model->frameAt(left.row() - 1).timestamp : 0.0;
            double prevR = (right.row() > 0) ? model->frameAt(right.row() - 1).timestamp : 0.0;
            return (fl.timestamp - prevL) < (fr.timestamp - prevR);
        }
        case SinceDisplay: {
            auto itL = m_displayDeltas.constFind(left.row());
            auto itR = m_displayDeltas.constFind(right.row());
            double dl = (itL != m_displayDeltas.constEnd()) ? itL.value() : fl.timestamp;
            double dr = (itR != m_displayDeltas.constEnd()) ? itR.value() : fr.timestamp;
            return dl < dr;
        }
        default:
            // Absolute / DateTimeOfDay / SecondsSinceEpoch: 仅显示格式不同，值仍为绝对时间戳
            return fl.timestamp < fr.timestamp;
        }
    case CanTraceModel::ColDelta: {
        double dl = (left.row() > 0) ? fl.timestamp - model->frameAt(left.row() - 1).timestamp : 0.0;
        double dr = (right.row() > 0) ? fr.timestamp - model->frameAt(right.row() - 1).timestamp : 0.0;
        return dl < dr;
    }
    case CanTraceModel::ColChannel:
        return fl.channel < fr.channel;
    case CanTraceModel::ColDirection:
        return fl.direction < fr.direction;
    case CanTraceModel::ColId:
        return fl.id < fr.id;
    case CanTraceModel::ColDlc:
        return fl.dlc < fr.dlc;
    case CanTraceModel::ColData:
        return fl.data < fr.data;
    case CanTraceModel::ColFlags:
        return CanUtils::formatFlags(fl) < CanUtils::formatFlags(fr);
    case CanTraceModel::ColFrameCount:
        return model->frameCountForId(fl.id) < model->frameCountForId(fr.id);
    }

    return QSortFilterProxyModel::lessThan(left, right);
}

// ============================================================
//  时间戳显示模式
// ============================================================

void CanFilterProxyModel::setTimestampMode(TimestampMode mode)
{
    if (m_timestampMode == mode)
        return;
    m_timestampMode = mode;

    if (mode == SinceDisplay)
        recomputeDisplayDeltas();

    // 刷新 Time 列所有可见行
    int rows = rowCount();
    if (rows > 0) {
        emit dataChanged(index(0, CanTraceModel::ColTime),
                         index(rows - 1, CanTraceModel::ColTime),
                         {Qt::DisplayRole});
    }

    // 切换显示模式后排序键可能变化（SinceCapture/SinceDisplay 显示的是增量），
    // 若当前按 Time 列排序则需重新排序
    if (sortColumn() == CanTraceModel::ColTime)
        sort(CanTraceModel::ColTime, sortOrder());
}

void CanFilterProxyModel::setTimePrecision(int precision)
{
    if (m_timePrecision == precision)
        return;
    m_timePrecision = precision;

    // 刷新 Time 列和 Delta 列所有可见行
    int rows = rowCount();
    if (rows > 0) {
        emit dataChanged(index(0, CanTraceModel::ColTime),
                         index(rows - 1, CanTraceModel::ColTime),
                         {Qt::DisplayRole});
        emit dataChanged(index(0, CanTraceModel::ColDelta),
                         index(rows - 1, CanTraceModel::ColDelta),
                         {Qt::DisplayRole});
    }
}

int CanFilterProxyModel::capturedCount() const
{
    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    return model ? model->frameCount() : 0;
}

void CanFilterProxyModel::invalidateFilter()
{
    QSortFilterProxyModel::invalidateFilter();
    if (m_timestampMode == SinceDisplay)
        recomputeDisplayDeltas();
    emitPacketCount();
}

void CanFilterProxyModel::recomputeDisplayDeltas()
{
    m_displayDeltas.clear();
    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    if (!model)
        return;

    // Phase 3: 使用 filterAcceptsRow 替代 mapFromSource，避免 O(log n) 映射开销
    double prevTime = 0.0;
    bool first = true;
    int total = model->rowCount();
    for (int i = 0; i < total; ++i) {
        if (filterAcceptsRow(i, QModelIndex())) {
            double t = model->frameAt(i).timestamp;
            m_displayDeltas[i] = first ? t : (t - prevTime);
            prevTime = t;
            first = false;
        }
    }
}

void CanFilterProxyModel::emitPacketCount()
{
    emit packetCountChanged(capturedCount(), displayedCount());
}

QVariant CanFilterProxyModel::data(const QModelIndex &proxyIndex, int role) const
{
    if (role == Qt::DisplayRole && proxyIndex.column() == CanTraceModel::ColTime) {
        auto *model = qobject_cast<CanTraceModel *>(sourceModel());
        if (!model)
            return {};
        QModelIndex sourceIdx = mapToSource(proxyIndex);
        if (!sourceIdx.isValid())
            return {};
        const CanFrame &f = model->frameAt(sourceIdx.row());
        int prec = m_timePrecision;

        switch (m_timestampMode) {
        case Absolute:
            return CanUtils::formatTime(f.timestamp, prec);
        case SinceCapture: {
            double prev = (sourceIdx.row() > 0)
                ? model->frameAt(sourceIdx.row() - 1).timestamp : 0.0;
            return CanUtils::formatTime(f.timestamp - prev, prec);
        }
        case SinceDisplay: {
            auto it = m_displayDeltas.find(sourceIdx.row());
            if (it != m_displayDeltas.end())
                return CanUtils::formatTime(it.value(), prec);
            // 未命中缓存，实时计算并缓存
            double prevTime = 0.0;
            bool found = false;
            for (int r = sourceIdx.row() - 1; r >= 0; --r) {
                if (filterAcceptsRow(r, QModelIndex())) {
                    prevTime = model->frameAt(r).timestamp;
                    found = true;
                    break;
                }
            }
            double delta = found ? (f.timestamp - prevTime) : f.timestamp;
            m_displayDeltas[sourceIdx.row()] = delta;
            return CanUtils::formatTime(delta, prec);
        }
        case DateTimeOfDay:
            return CanUtils::formatDateTime(model->captureStartTime(), f.timestamp, prec);
        case SecondsSinceEpoch: {
            QDateTime start = model->captureStartTime();
            if (start.isValid())
                return CanUtils::formatTime(start.toMSecsSinceEpoch() / 1000.0 + f.timestamp, prec);
            return CanUtils::formatTime(f.timestamp, prec);
        }
        }
    }
    return QSortFilterProxyModel::data(proxyIndex, role);
}
