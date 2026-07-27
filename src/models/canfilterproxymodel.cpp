#include "canfilterproxymodel.h"
#include "cantracemodel.h"

CanFilterProxyModel::CanFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

bool CanFilterProxyModel::setFilterExpression(const QString &expr)
{
    m_expr = expr;
    if (expr.trimmed().isEmpty()) {
        m_predicate.reset();
        invalidateFilter();
        return true;
    }
    if (!CanUtils::isFilterValid(expr)) {
        m_predicate.reset();
        invalidateFilter();
        return false;
    }
    m_predicate = CanUtils::parseFilter(expr);
    invalidateFilter();
    return true;
}

void CanFilterProxyModel::clearFilter()
{
    m_expr.clear();
    m_predicate.reset();
    invalidateFilter();
}

bool CanFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (!m_predicate.has_value())
        return true; // 无过滤

    auto *model = qobject_cast<CanTraceModel *>(sourceModel());
    if (!model)
        return true;

    const CanFrame &frame = model->frameAt(sourceRow);
    return (*m_predicate)(frame);
}
