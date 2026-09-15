#include "viewportproxy.h"

ViewportProxyModel::ViewportProxyModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

void ViewportProxyModel::notifyViewportContentChanged()
{
    const int n = rowCount();
    if (n <= 0)
        return;
    emit dataChanged(index(0, 0), index(n - 1, columnCount() - 1));
}

void ViewportProxyModel::setViewportStart(int start)
{
    start = clampStart(start);
    if (start == m_viewportStart)
        return;

    m_viewportStart = start;
    int newRowCount = rowCount();
    int cols = columnCount();

    if (m_lastReportedRowCount == newRowCount && newRowCount > 0) {
        // Same window length — refresh cells only (keeps selection / inner scroll).
        notifyViewportContentChanged();
    } else if (newRowCount > m_lastReportedRowCount) {
        if (m_lastReportedRowCount > 0)
            emit dataChanged(index(0, 0), index(m_lastReportedRowCount - 1, cols - 1));
        beginInsertRows({}, m_lastReportedRowCount, newRowCount - 1);
        endInsertRows();
    } else if (newRowCount < m_lastReportedRowCount) {
        if (newRowCount > 0)
            emit dataChanged(index(0, 0), index(newRowCount - 1, cols - 1));
        if (m_lastReportedRowCount > newRowCount) {
            beginRemoveRows({}, newRowCount, m_lastReportedRowCount - 1);
            endRemoveRows();
        }
    } else {
        beginResetModel();
        endResetModel();
    }

    m_lastReportedRowCount = newRowCount;
    emit viewportChanged();
}

void ViewportProxyModel::setViewportSize(int size)
{
    if (size < 1)
        size = 1;
    if (size == m_viewportSize)
        return;

    beginResetModel();
    m_viewportSize = size;
    m_viewportStart = clampStart(m_viewportStart);
    endResetModel();
    m_lastReportedRowCount = rowCount();
    emit viewportChanged();
}

int ViewportProxyModel::sourceRowCount() const
{
    auto *src = sourceModel();
    return src ? src->rowCount() : 0;
}

int ViewportProxyModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    int srcRows = sourceRowCount();
    if (srcRows == 0)
        return 0;
    int available = srcRows - m_viewportStart;
    return qMin(m_viewportSize, available);
}

int ViewportProxyModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    auto *src = sourceModel();
    return src ? src->columnCount() : 0;
}

QModelIndex ViewportProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid())
        return {};
    int sourceRow = proxyIndex.row() + m_viewportStart;
    auto *src = sourceModel();
    if (!src || sourceRow >= src->rowCount())
        return {};
    return src->index(sourceRow, proxyIndex.column());
}

QModelIndex ViewportProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid())
        return {};
    int proxyRow = sourceIndex.row() - m_viewportStart;
    if (proxyRow < 0 || proxyRow >= rowCount())
        return {};
    return createIndex(proxyRow, sourceIndex.column());
}

QModelIndex ViewportProxyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid())
        return {};
    if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

QModelIndex ViewportProxyModel::parent(const QModelIndex &child) const
{
    Q_UNUSED(child);
    return {};
}

QModelIndex ViewportProxyModel::sibling(int row, int column, const QModelIndex &idx) const
{
    if (!idx.isValid())
        return {};
    if (row < 0 || row >= rowCount() || column < 0 || column >= columnCount())
        return {};
    return createIndex(row, column);
}

void ViewportProxyModel::sort(int column, Qt::SortOrder order)
{
    if (!sourceModel())
        return;

    m_sorting = true;
    emit layoutAboutToBeChanged();
    sourceModel()->sort(column, order);
    m_viewportStart = clampStart(m_viewportStart);
    m_lastReportedRowCount = rowCount();
    emit layoutChanged();
    m_sorting = false;
    emit viewportChanged();
}

void ViewportProxyModel::ensureVisible(int sourceRow)
{
    if (sourceRow < 0)
        return;

    if (sourceRow >= m_viewportStart && sourceRow < m_viewportStart + rowCount())
        return;

    int newStart = sourceRow - m_viewportSize / 2;
    setViewportStart(clampStart(newStart));
}

void ViewportProxyModel::scrollToEnd()
{
    int srcRows = sourceRowCount();
    if (srcRows == 0)
        return;
    int newStart = srcRows - qMin(m_viewportSize, srcRows);
    setViewportStart(newStart);
}

int ViewportProxyModel::clampStart(int start) const
{
    if (start < 0)
        return 0;
    int srcRows = sourceRowCount();
    if (srcRows == 0)
        return 0;
    int maxStart = qMax(0, srcRows - qMin(m_viewportSize, srcRows));
    return qMin(start, maxStart);
}

void ViewportProxyModel::adjustOnStructuralChange()
{
    const int oldStart = m_viewportStart;
    m_viewportStart = clampStart(m_viewportStart);
    int newRowCount = rowCount();
    int cols = columnCount();

    if (newRowCount > m_lastReportedRowCount) {
        if (m_lastReportedRowCount > 0)
            emit dataChanged(index(0, 0), index(m_lastReportedRowCount - 1, cols - 1));
        beginInsertRows({}, m_lastReportedRowCount, newRowCount - 1);
        endInsertRows();
    } else if (newRowCount < m_lastReportedRowCount) {
        if (newRowCount > 0)
            emit dataChanged(index(0, 0), index(newRowCount - 1, cols - 1));
        if (m_lastReportedRowCount > newRowCount) {
            beginRemoveRows({}, newRowCount, m_lastReportedRowCount - 1);
            endRemoveRows();
        }
    } else if (newRowCount > 0) {
        // Fixed window length: either slid (follow end) or content changed in place.
        Q_UNUSED(oldStart);
        notifyViewportContentChanged();
    }

    m_lastReportedRowCount = newRowCount;
    emit viewportChanged();
}

void ViewportProxyModel::onSourceDataChanged(const QModelIndex &topLeft,
                                              const QModelIndex &bottomRight,
                                              const QVector<int> &roles)
{
    int proxyTop = topLeft.row() - m_viewportStart;
    int proxyBottom = bottomRight.row() - m_viewportStart;
    int rc = rowCount();
    if (proxyBottom < 0 || proxyTop >= rc)
        return;

    proxyTop = qMax(0, proxyTop);
    proxyBottom = qMin(rc - 1, proxyBottom);
    if (proxyTop > proxyBottom)
        return;

    emit dataChanged(index(proxyTop, topLeft.column()),
                     index(proxyBottom, bottomRight.column()), roles);
}

void ViewportProxyModel::onSourceHeaderDataChanged(Qt::Orientation orientation,
                                                     int first, int last)
{
    emit headerDataChanged(orientation, first, last);
}

void ViewportProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (sourceModel == this->sourceModel())
        return;

    beginResetModel();

    if (this->sourceModel())
        disconnect(this->sourceModel(), nullptr, this, nullptr);

    QAbstractProxyModel::setSourceModel(sourceModel);
    m_viewportStart = 0;

    if (sourceModel) {
        connect(sourceModel, &QAbstractItemModel::dataChanged,
                this, &ViewportProxyModel::onSourceDataChanged);
        connect(sourceModel, &QAbstractItemModel::headerDataChanged,
                this, &ViewportProxyModel::onSourceHeaderDataChanged);
        connect(sourceModel, &QAbstractItemModel::rowsInserted,
                this, [this](const QModelIndex &, int, int) {
            adjustOnStructuralChange();
        });
        connect(sourceModel, &QAbstractItemModel::rowsRemoved,
                this, [this](const QModelIndex &, int, int) {
            adjustOnStructuralChange();
        });
        connect(sourceModel, &QAbstractItemModel::modelReset,
                this, [this]() {
            beginResetModel();
            m_viewportStart = 0;
            endResetModel();
            m_lastReportedRowCount = rowCount();
            emit viewportChanged();
        });
        connect(sourceModel, &QAbstractItemModel::layoutAboutToBeChanged,
                this, [this]() {
            if (!m_sorting)
                emit layoutAboutToBeChanged();
        });
        connect(sourceModel, &QAbstractItemModel::layoutChanged,
                this, [this]() {
            if (m_sorting)
                return;
            m_viewportStart = clampStart(m_viewportStart);
            m_lastReportedRowCount = rowCount();
            emit layoutChanged();
            emit viewportChanged();
        });
    }

    endResetModel();
    m_lastReportedRowCount = rowCount();
    emit viewportChanged();
}
