#ifndef CANTRACEPROXYMODEL_H
#define CANTRACEPROXYMODEL_H

#include <QAbstractProxyModel>
#include <QHash>
#include <QSet>
#include <QVector>
#include <functional>
#include <memory>
#include "core/filter_engine.h"

class CanTraceModel;

/**
 * @brief CAN 报文增量过滤代理模型（Phase 3）
 *
 * 替代 CanFilterProxyModel（QSortFilterProxyModel）。
 * QSortFilterProxyModel 在 invalidateFilter() 时对全部行重评估；
 * 本代理仅评估新增行，已有行的过滤结果通过映射表保留：
 *
 * - 新增行：仅评估新行，追加到映射尾部（O(1)/行）
 * - 过滤条件变化：单次全量遍历重评估 + layoutChanged
 * - 排序：独立排序索引（m_proxyRows），不动源模型行号
 * - SinceDisplay 增量：由映射直接推导，无需全量重算
 *
 * 模型链: CanTraceModel → CanTraceProxyModel → ViewportProxyModel → TraceView
 */
class CanTraceProxyModel : public QAbstractProxyModel
{
    Q_OBJECT

public:
    /// 时间戳显示模式（对标 Wireshark View → Time Display Format）
    enum TimestampMode {
        Absolute = 0,       ///< 自捕获开始的秒数
        SinceCapture,       ///< 自上一个捕获分组经过的时间
        SinceDisplay,       ///< 自上一个显示分组经过的时间
        DateTimeOfDay,      ///< 日期+时间 "yyyy-MM-dd HH:mm:ss.zzzzzz"
        SecondsSinceEpoch   ///< Unix epoch 秒数
    };
    Q_ENUM(TimestampMode)

    explicit CanTraceProxyModel(QObject *parent = nullptr);

    // ---- 主过滤表达式 ----

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

    // ---- 值集过滤（Excel 风格复选框） ----

    /// 设置某列的值集过滤（只显示选中的值）
    void setColumnFilterValues(int column, const QSet<QString> &values);

    /// 清除某列的值集过滤
    void clearColumnFilterValues(int column);

    /// 获取某列已选中的值集
    QSet<QString> columnFilterValues(int column) const { return m_columnFilterValues.value(column); }

    /// 某列是否有值集过滤
    bool hasColumnFilterValues(int column) const { return m_columnFilterValues.contains(column); }

    // ---- 时间戳显示模式 ----

    /// 设置时间戳显示模式，切换后自动刷新 Time 列
    void setTimestampMode(TimestampMode mode);
    TimestampMode timestampMode() const { return m_timestampMode; }

    /// 设置时间显示精度（-1=自动, 0=秒, 3=毫秒, 6=微秒, 9=纳秒）
    void setTimePrecision(int precision);
    int timePrecision() const { return m_timePrecision; }

    // ---- 分组统计 ----

    /// 捕获分组数（源模型总行数）
    int capturedCount() const;
    /// 显示分组数（过滤后行数）
    int displayedCount() const { return m_proxyRows.size(); }

    /// 手动触发分组计数信号（新增帧后调用）
    void emitPacketCount();

    // ---- 排序 ----

    void sort(int column, Qt::SortOrder order) override;
    int sortColumn() const { return m_sortColumn; }
    Qt::SortOrder sortOrder() const { return m_sortOrder; }

    // ---- QAbstractProxyModel ----
    void setSourceModel(QAbstractItemModel *sourceModel) override;
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;
    QVariant data(const QModelIndex &proxyIndex, int role = Qt::DisplayRole) const override;

    // ---- QAbstractItemModel ----
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;

signals:
    /// 分组数量发生变化（新增帧或过滤条件变化后发出）
    void packetCountChanged(int captured, int displayed);

private slots:
    void onSourceRowsInserted(const QModelIndex &parent, int first, int last);
    void onSourceRowsRemoved(const QModelIndex &parent, int first, int last);
    void onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                             const QVector<int> &roles);
    void onSourceRingWrapped(int shift);
    void onSourceModelAboutToBeReset();
    void onSourceModelReset();

private:
    QString m_expr;
    std::unique_ptr<FilterEngine> m_filterEngine;

    QHash<int, QString> m_columnFilters;       ///< column -> 过滤文本
    QHash<int, QSet<QString>> m_columnFilterValues;  ///< column -> 选中的值集合（Excel 风格）
    TimestampMode m_timestampMode = Absolute;
    int m_timePrecision = 6;  ///< 时间显示精度（-1=auto, 0=秒, 3=毫秒, 6=微秒, 9=纳秒）

    // ---- Phase 3: 双向映射 ----
    QVector<int> m_proxyRows;    ///< 代理行号 → 源模型行号（排序时为排序序）
    QVector<int> m_sourceToProxy;  ///< 源模型行号 → 代理行号（-1 = 被过滤）
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    /// SinceDisplay 模式显示增量：由映射 O(1) 实时推导（data() 路径）
    quint64 m_lastSeq = 0;             ///< 上次同步时源模型的 seqCounter（检测环形覆盖）

    /// Time+SinceDisplay 排序的增量快照（用户需求 2026-08-24：支持按显示
    /// 分组排序）。增量依赖显示顺序、显示顺序又依赖排序 → 循环依赖；
    /// 解法：进入该排序时按当时显示序拍快照作排序键，此后显示值沿用
    /// 快照（列表顺序与显示值严格对应，值不随排序重排漂移；效果等同
    /// 把"间隔"当普通列值排序）。取消排序/切换模式/改过滤时快照重建或作废。
    bool m_deltaSortFrozen = false;    ///< 增量快照冻结模式激活中
    QVector<double> m_displayDeltaKeys; ///< 源行号 → 增量快照（排序键 = 冻结显示值）
    int m_lastAcceptedSourceRow = -1;  ///< 源序上一显示行（新帧增量推导）

    CanTraceModel *traceModel() const;

    /// 主表达式 + 列过滤 + 值集过滤 的综合判定
    bool filterAcceptsRow(int sourceRow) const;
    bool matchColumnFilter(int sourceRow, int column) const;
    bool matchColumnFilterValues(int sourceRow, int column) const;
    QString columnDisplayText(int sourceRow, int column) const;

    /// 排序比较（按 m_sortColumn），相等时按源行号保证稳定
    bool lessThan(int sourceLeft, int sourceRight) const;
    /// SinceDisplay 模式下源行的时间增量（由映射 O(1) 推导）
    double displayDelta(int sourceRow) const;
    /// 增量快照冻结激活（Time+SinceDisplay 排序中：显示值=排序键快照）
    bool deltaSortFrozen() const { return m_deltaSortFrozen; }
    /// 按当前代理序刷新增量快照（进入冻结态前调用；快照=此刻显示值）
    void refreshDeltaKeys();
    /// 源行的增量快照键（无快照时退化为 0）
    double deltaKey(int sourceRow) const;
    /// DBC 报文名（Name 列排序/过滤用）
    QString messageName(int sourceRow) const;

    /// 过滤/重置后全量重建映射（O(n)，单次，含持久索引重映射）
    void rebuildMapping();
    /// 由 m_proxyRows 重建 m_sourceToProxy（O(n)）
    void rebuildSourceToProxy();

    /// layoutChanged 事务：捕获持久索引 → layoutAboutToBeChanged → mutate → 重映射持久索引 → layoutChanged
    void withLayoutChange(const std::function<void()> &mutate);
    /// 无信号重建映射（须处于 reset/layout 事务内调用）
    void buildMapping();
    /// 按当前排序列/方向重排 m_proxyRows（含持久索引重映射）
    void resortCurrent();
    /// 转发源数据变化（源行区间 → 代理行区间，中间被过滤行会导致多刷，无碍）
    void forwardDataChanged(int srcTop, int srcBottom, const QVector<int> &roles);

    /// 环形缓冲区覆盖（shift>0）：内容整体前移，accept(i) = accept_old(i+shift)
    void handleFullShift(int shift, const QVector<int> &roles);
};

#endif // CANTRACEPROXYMODEL_H
