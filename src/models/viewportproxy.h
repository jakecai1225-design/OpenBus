#ifndef VIEWPORTPROXY_H
#define VIEWPORTPROXY_H

#include <QAbstractProxyModel>

/**
 * @brief Fixed-size Trace viewport proxy (CANoe-style / T3).
 *
 * Exposes only source rows [viewportStart, viewportStart + viewportSize).
 * QTableView scrolls inside this window; overview / external bar move the window
 * across the filtered source (CanTraceProxyModel).
 *
 * T3: viewportSize defaults to ~cacheRows (not thousands) so paint/notify cost
 * stays O(screen), independent of CaptureLog history length.
 *
 * Model chain:
 *   CanTraceModel → CanTraceProxyModel → ViewportProxyModel → QTableView
 */
class ViewportProxyModel : public QAbstractProxyModel
{
    Q_OBJECT

public:
    explicit ViewportProxyModel(QObject *parent = nullptr);

    void setViewportStart(int start);
    int viewportStart() const { return m_viewportStart; }

    void setViewportSize(int size);
    int viewportSize() const { return m_viewportSize; }

    /// Filtered source row count (overview / scrollbar range).
    int sourceRowCount() const;

    void ensureVisible(int sourceRow);
    void scrollToEnd();

    void setSourceModel(QAbstractItemModel *sourceModel) override;
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    QModelIndex sibling(int row, int column, const QModelIndex &idx) const override;
    void sort(int column, Qt::SortOrder order) override;

signals:
    void viewportChanged();

private slots:
    void onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                            const QVector<int> &roles);
    void onSourceHeaderDataChanged(Qt::Orientation orientation, int first, int last);

private:
    int m_viewportStart = 0;
    int m_viewportSize = 128;       ///< T3 default ≈ trace.cacheRows
    int m_lastReportedRowCount = 0;
    bool m_sorting = false;

    int clampStart(int start) const;
    void adjustOnStructuralChange();
    void notifyViewportContentChanged();
};

#endif // VIEWPORTPROXY_H
