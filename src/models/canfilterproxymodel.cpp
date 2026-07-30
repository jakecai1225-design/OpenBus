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
    invalidateFilter();
}

bool CanFilterProxyModel::hasColumnFilter(int column) const
{
    return m_columnFilters.contains(column);
}

bool CanFilterProxyModel::matchColumnFilter(int sourceRow, int column) const
{
    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    if (!model) return true;

    const CanFrame &frame = model->frameAt(sourceRow);
    QString filter = m_columnFilters.value(column).toLower();

    switch (column) {
    case CanTraceModel::ColTime: {
        // 支持 ">0.5", "<1.0", "0.3~0.8" 等范围
        QString val = CanUtils::formatTime(frame.timestamp);
        if (filter.startsWith(">"))
            return frame.timestamp > filter.mid(1).trimmed().toDouble();
        if (filter.startsWith("<"))
            return frame.timestamp < filter.mid(1).trimmed().toDouble();
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
    }
    return true;
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
    case CanTraceModel::ColTime:
        return fl.timestamp < fr.timestamp;
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
    }

    return QSortFilterProxyModel::lessThan(left, right);
}
