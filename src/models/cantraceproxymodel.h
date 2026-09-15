#ifndef CANTRACEPROXYMODEL_H
#define CANTRACEPROXYMODEL_H

#include <QAbstractProxyModel>
#include <QHash>
#include <QSet>
#include <QVector>
#include <functional>
#include <memory>
#include "core/filter_engine.h"
#include "core/tracefilterindex.h"

class CanTraceModel;

/**
 * @brief Incremental Trace filter proxy (Phase 3 + T2 lean maps).
 *
 * Mapping modes (T2):
 * - Passthrough: no filters, capture order — identity map, no per-history vectors
 * - AcceptIndex: filters on, capture order — TraceFilterIndex of accepted rows only;
 *   reverse lookup via binary search (no dense source→proxy)
 * - DenseMaps: user sort active — accepted list + dense reverse map (legacy path)
 *
 * Model chain: CanTraceModel → CanTraceProxyModel → ViewportProxyModel → TraceView
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
    /// Displayed (accepted) row count
    int displayedCount() const;

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
    int m_timePrecision = 6;  ///< Time display precision (-1=auto, 0=s, 3=ms, 6=us, 9=ns)

    /// T2 mapping mode — avoids O(history) dense reverse maps on the live path.
    enum class MapMode {
        Passthrough,  ///< No filters; proxy row == source row
        AcceptIndex,  ///< Filter index only (capture order)
        DenseMaps     ///< Sorted view: index + dense reverse map
    };
    MapMode m_mapMode = MapMode::Passthrough;
    TraceFilterIndex m_acceptIndex; ///< Accepted source rows (proxy order)
    QVector<int> m_sourceToProxy;   ///< Dense reverse map; empty unless DenseMaps

    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    /// SinceDisplay: derived from mapping (data() path)
    quint64 m_lastSeq = 0;

    /// Time+SinceDisplay sort snapshot (frozen display deltas as sort keys)
    bool m_deltaSortFrozen = false;
    QVector<double> m_displayDeltaKeys;
    int m_lastAcceptedSourceRow = -1;

    CanTraceModel *traceModel() const;

    bool hasActiveFilters() const;
    bool isAppendOnlyOrder() const;
    MapMode computeMapMode() const;
    int proxyRowCount() const;
    int sourceRowAtProxy(int proxyRow) const;
    int proxyRowOfSource(int sourceRow) const;

    bool filterAcceptsRow(int sourceRow) const;
    bool matchColumnFilter(int sourceRow, int column) const;
    bool matchColumnFilterValues(int sourceRow, int column) const;
    QString columnDisplayText(int sourceRow, int column) const;

    bool lessThan(int sourceLeft, int sourceRight) const;
    double displayDelta(int sourceRow) const;
    bool deltaSortFrozen() const { return m_deltaSortFrozen; }
    void refreshDeltaKeys();
    double deltaKey(int sourceRow) const;
    QString messageName(int sourceRow) const;

    void rebuildMapping();
    void rebuildSourceToProxy();

    void withLayoutChange(const std::function<void()> &mutate);
    void buildMapping();
    void resortCurrent();
    void forwardDataChanged(int srcTop, int srcBottom, const QVector<int> &roles);

    void handleFullShift(int shift, const QVector<int> &roles);
};

#endif // CANTRACEPROXYMODEL_H
