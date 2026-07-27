#ifndef CANFILTERPROXYMODEL_H
#define CANFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>
#include <optional>
#include "utils/canutils.h"

/**
 * @brief CAN 报文过滤代理模型
 *
 * 使用 CanUtils::FilterPredicate 对 CanTraceModel 进行行级过滤。
 * Wireshark 风格：输入过滤表达式后实时筛选显示。
 */
class CanFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit CanFilterProxyModel(QObject *parent = nullptr);

    /// 设置过滤表达式，返回表达式是否合法
    bool setFilterExpression(const QString &expr);

    /// 当前过滤表达式
    QString filterExpression() const { return m_expr; }

    /// 过滤是否生效（表达式非空且合法）
    bool filterActive() const { return m_predicate.has_value(); }

    /// 清除过滤
    void clearFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_expr;
    std::optional<CanUtils::FilterPredicate> m_predicate;
};

#endif // CANFILTERPROXYMODEL_H
