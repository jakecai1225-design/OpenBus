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
 * 支持 Wireshark 风格的时间戳显示模式切换。
 */
class CanFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    /// 时间戳显示模式（对标 Wireshark View → Time Display Format）
    enum TimestampMode {
        Absolute = 0,       ///< 绝对时间戳（自捕获开始）
        SinceCapture,       ///< 自上一个捕获分组经过的时间
        SinceDisplay        ///< 自上一个显示分组经过的时间
    };
    Q_ENUM(TimestampMode)

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

    // ---- 时间戳显示模式 ----

    /// 设置时间戳显示模式，切换后自动刷新 Time 列
    void setTimestampMode(TimestampMode mode);
    TimestampMode timestampMode() const { return m_timestampMode; }

    // ---- 分组统计 ----

    /// 捕获分组数（源模型总行数）
    int capturedCount() const;
    /// 显示分组数（过滤后行数）
    int displayedCount() const { return rowCount(); }

    // ---- QAbstractProxyModel ----
    QVariant data(const QModelIndex &proxyIndex, int role = Qt::DisplayRole) const override;

    /// 手动触发分组计数信号（新增帧后调用）
    void emitPacketCount();

signals:
    /// 分组数量发生变化（新增帧或过滤条件变化后发出）
    void packetCountChanged(int captured, int displayed);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

    /// 过滤条件变化后重新计算显示增量（隐藏基类同名方法，非 virtual）
    void invalidateFilter();

private:
    QString m_expr;
    std::unique_ptr<FilterEngine> m_filterEngine;

    QHash<int, QString> m_columnFilters;  // column -> filter text
    TimestampMode m_timestampMode = Absolute;

    /// SinceDisplay 模式：源模型行号 → 与上一个显示帧的时间增量
    QHash<int, double> m_displayDeltas;

    bool matchColumnFilter(int sourceRow, int column) const;
    /// 重新计算 SinceDisplay 模式下每个显示帧的增量
    void recomputeDisplayDeltas();
};

#endif // CANFILTERPROXYMODEL_H
