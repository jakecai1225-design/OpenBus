#ifndef FILTERHEADERVIEW_H
#define FILTERHEADERVIEW_H

#include <QHeaderView>

class CanFilterProxyModel;

/**
 * @brief Wireshark 风格表头视图 — 鼠标悬停显示漏斗过滤图标
 *
 * 功能:
 * - 鼠标悬停某列表头时，右侧绘制一个半透明漏斗图标
 * - 该列已有过滤条件时，漏斗图标持续显示并高亮
 * - 点击漏斗图标触发 filterClicked 信号 → 弹出列筛选对话框
 * - 漏斗图标左侧绘制一个小三角形，点击触发排序切换
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
    int m_hoverSection = -1;  ///< 当前鼠标悬停的逻辑列号

    /// 获取漏斗图标的绘制区域
    QRect filterRect(const QRect &sectionRect) const;
    /// 检查鼠标位置是否在漏斗图标上
    int sectionAtFilter(const QPoint &pos) const;
    /// 绘制漏斗图标
    void drawFilterIcon(QPainter *painter, const QRect &rect, bool active, bool hovered) const;
};

#endif // FILTERHEADERVIEW_H
