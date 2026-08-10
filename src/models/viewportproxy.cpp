#include "viewportproxy.h"

#include <QDebug>

ViewportProxyModel::ViewportProxyModel(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

void ViewportProxyModel::setViewportStart(int start)
{
    start = clampStart(start);
    if (start == m_viewportStart)
        return;

    beginResetModel();
    m_viewportStart = start;
    endResetModel();
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

void ViewportProxyModel::ensureVisible(int sourceRow)
{
    if (sourceRow < 0)
        return;

    // 已在视窗内
    if (sourceRow >= m_viewportStart && sourceRow < m_viewportStart + rowCount())
        return;

    // 移动视窗使该行可见 — 尽量居中
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
    int newStart = clampStart(m_viewportStart);
    if (newStart != m_viewportStart) {
        beginResetModel();
        m_viewportStart = newStart;
        endResetModel();
    }
}

void ViewportProxyModel::onSourceDataChanged(const QModelIndex &topLeft,
                                              const QModelIndex &bottomRight,
                                              const QVector<int> &roles)
{
    // 将源模型行号映射到视窗内行号
    int proxyTop = topLeft.row() - m_viewportStart;
    int proxyBottom = bottomRight.row() - m_viewportStart;
    int rc = rowCount();
    if (proxyBottom < 0 || proxyTop >= rc)
        return;  // 变化区域不在视窗内

    // 裁剪到视窗范围
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

// ---- setSourceModel: 连接信号 ----

void ViewportProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (sourceModel == this->sourceModel())
        return;

    beginResetModel();

    // 断开旧源模型信号
    if (this->sourceModel()) {
        disconnect(this->sourceModel(), nullptr, this, nullptr);
    }

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
        });
        connect(sourceModel, &QAbstractItemModel::layoutChanged,
                this, [this]() {
            // 排序变化后调整视窗
            adjustOnStructuralChange();
        });
    }

    endResetModel();
}
