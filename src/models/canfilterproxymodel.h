#ifndef CANFILTERPROXYMODEL_H
#define CANFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>
#include <QHash>
#include <memory>
#include "core/filter_engine.h"

/**
 * @brief CAN 报文过滤代理模型
 *
 * 使用 FilterEngine 对 CanTraceModel 进行行级过滤。
 * 支持主过滤表达式 + 按列子过滤器。
 */
class CanFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit CanFilterProxyModel(QObject *parent = nullptr);

    /// 设置主过滤表达式，返回表达式是否合法
    bool setFilterExpression(const QString &expr);

    /// 当前主过滤表达式
    QString filterExpression() const { return m_expr; }

    /// 主过滤是否生效
    bool filterActive() const { return m_filterEngine && m_filterEngine->isValid() && !m_filterEngine->isEmpty(); }

    /// 清除主过滤
    void clearFilter();

    // ---- 按列过滤 ----

    /// 设置某列的过滤文本（简单包含匹配）
    void setColumnFilter(int column, const QString &text);

    /// 清除某列的过滤
    void clearColumnFilter(int column);

    /// 清除所有列过滤
    void clearAllColumnFilters();

    /// 某列是否有活跃的列过滤
    bool hasColumnFilter(int column) const;

    /// 获取某列的过滤文本
    QString columnFilter(int column) const { return m_columnFilters.value(column); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    QString m_expr;
    std::unique_ptr<FilterEngine> m_filterEngine;

    QHash<int, QString> m_columnFilters;  // column -> filter text

    bool matchColumnFilter(int sourceRow, int column) const;
};

#endif // CANFILTERPROXYMODEL_H
