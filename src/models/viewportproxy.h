#ifndef VIEWPORTPROXY_H
#define VIEWPORTPROXY_H

#include <QAbstractProxyModel>

/**
 * @brief CANoe 风格视窗代理模型
 *
 * 在 CanFilterProxyModel 之上叠加一层"固定行数视窗"限制：
 *   - 只暴露源模型 [viewportStart, viewportStart + viewportSize) 范围的行
 *   - QTableView 内置滚动条自然限制在视窗内
 *   - 外部 QScrollBar 控制视窗在全部数据中的位置
 *   - 筛选/排序在源模型 (CanFilterProxyModel) 层完成，对全部数据生效
 *
 * 模型链:
 *   CanTraceModel → CanFilterProxyModel → ViewportProxyModel → QTableView
 */
class ViewportProxyModel : public QAbstractProxyModel
{
    Q_OBJECT

public:
    explicit ViewportProxyModel(QObject *parent = nullptr);

    // ---- 视窗控制 ----

    /// 设置视窗起始行（在源模型中的行号）
    void setViewportStart(int start);
    int viewportStart() const { return m_viewportStart; }

    /// 设置视窗大小（固定行数）
    void setViewportSize(int size);
    int viewportSize() const { return m_viewportSize; }

    /// 源模型总行数（视窗可访问的范围上限）
    int sourceRowCount() const;

    /// 确保指定源行在视窗内可见（必要时移动视窗）
    void ensureVisible(int sourceRow);

    /// 将视窗移动到末尾（显示最后 viewportSize 行）
    void scrollToEnd();

    // ---- QAbstractProxyModel ----
    void setSourceModel(QAbstractItemModel *sourceModel) override;
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    // ---- QAbstractItemModel ----
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    QModelIndex sibling(int row, int column, const QModelIndex &idx) const override;

signals:
    /// 视窗位置或大小发生变化
    void viewportChanged();

private slots:
    void onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                            const QVector<int> &roles);
    void onSourceHeaderDataChanged(Qt::Orientation orientation, int first, int last);

private:
    int m_viewportStart = 0;
    int m_viewportSize = 2000;
    int m_lastReportedRowCount = 0;  ///< 视图已知的行数（用于检测行数变化）

    /// 将源模型行号限制在有效范围内
    int clampStart(int start) const;
    /// 源模型结构变化时调整视窗位置
    void adjustOnStructuralChange();
};

#endif // VIEWPORTPROXY_H
