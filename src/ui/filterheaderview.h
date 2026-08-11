#ifndef FILTERHEADERVIEW_H
#define FILTERHEADERVIEW_H

#include <QHeaderView>

class CanFilterProxyModel;

/**
 * @brief Wireshark 风格表头视图 — 排序箭头 + 过滤漏斗图标
 *
 * 功能:
 * - 自行绘制排序三角形（不使用 Qt 内置指示器），避免与过滤图标重叠
 * - 排序三角形在漏斗图标左侧，两者有明确间距
 * - 漏斗图标始终可见（浅灰），激活时蓝色高亮，悬停时加深
 * - 点击漏斗图标触发 filterClicked 信号 → 弹出列筛选对话框
 * - 点击非漏斗区域触发正常排序
 */
class FilterHeaderView : public QHeaderView
{
    Q_OBJECT

public:
    explicit FilterHeaderView(Qt::Orientation orientation, QWidget *parent = nullptr);

    /// 设置代理模型（用于查询各列过滤状态）
    void setProxyModel(CanFilterProxyModel *proxy);

    /// 某列是否有活跃的过滤条件
    bool hasFilter(int logicalIndex) const;

    /// 自定义排序状态（独立于 Qt 内置指示器）
    void setSortState(int column, Qt::SortOrder order);
    void clearSortState();
    int sortColumn() const { return m_sortColumn; }
    Qt::SortOrder sortOrder() const { return m_sortOrder; }

signals:
    /// 漏斗图标被点击
    void filterClicked(int logicalIndex);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    CanFilterProxyModel *m_proxy = nullptr;
    int m_hoverSection = -1;       ///< 当前鼠标悬停的逻辑列号
    int m_sortColumn = -1;         ///< 当前排序列（-1=未排序）
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // 布局常量
    static constexpr int kFilterSize = 14;   ///< 漏斗图标边长
    static constexpr int kSortSize = 9;       ///< 排序三角形边长
    static constexpr int kRightMargin = 4;   ///< 距右边缘间距
    static constexpr int kGap = 3;            ///< 排序与过滤图标间距

    /// 获取漏斗图标的绘制区域
    QRect filterRect(const QRect &sectionRect) const;
    /// 获取排序三角形的绘制区域
    QRect sortIndicatorRect(const QRect &sectionRect) const;
    /// 检查鼠标位置是否在漏斗图标上
    int sectionAtFilter(const QPoint &pos) const;
    /// 绘制漏斗图标
    void drawFilterIcon(QPainter *painter, const QRect &rect, bool active, bool hovered) const;
    /// 绘制排序三角形
    void drawSortIndicator(QPainter *painter, const QRect &rect, bool ascending) const;
};

#endif // FILTERHEADERVIEW_H
