#include "filterheaderview.h"
#include "models/canfilterproxymodel.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>

// ============================================================
//  构造
// ============================================================

FilterHeaderView::FilterHeaderView(Qt::Orientation orientation, QWidget *parent)
    : QHeaderView(orientation, parent)
{
    setAttribute(Qt::WA_Hover, true);
    setSectionsClickable(true);
    setSectionsMovable(true);
    setStretchLastSection(false);
    setSectionResizeMode(QHeaderView::Interactive);
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
}

void FilterHeaderView::setProxyModel(CanFilterProxyModel *proxy)
{
    m_proxy = proxy;
}

bool FilterHeaderView::hasFilter(int logicalIndex) const
{
    return m_proxy && m_proxy->hasColumnFilter(logicalIndex);
}

// ============================================================
//  漏斗图标区域计算
// ============================================================

QRect FilterHeaderView::filterRect(const QRect &sectionRect) const
{
    // 漏斗图标放在列右侧，16x16 区域，垂直居中
    const int iconSize = 14;
    int x = sectionRect.right() - iconSize - 4;
    int y = sectionRect.top() + (sectionRect.height() - iconSize) / 2;
    return QRect(x, y, iconSize, iconSize);
}

int FilterHeaderView::sectionAtFilter(const QPoint &pos) const
{
    int visual = visualIndexAt(pos.x());
    if (visual < 0)
        return -1;
    int logical = logicalIndex(visual);
    if (logical < 0)
        return -1;
    // 检查鼠标是否在漏斗图标区域
    QRect secRect = QRect(sectionViewportPosition(logical), 0,
                          sectionSize(logical), height());
    QRect fRect = filterRect(secRect);
    if (fRect.contains(pos))
        return logical;
    return -1;
}

// ============================================================
//  绘制
// ============================================================

void FilterHeaderView::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    painter->save();
    // 先让基类绘制背景和文字
    QHeaderView::paintSection(painter, rect, logicalIndex);
    painter->restore();

    if (rect.width() < 30)
        return;  // 太窄不画漏斗

    bool active = hasFilter(logicalIndex);
    bool hovered = (logicalIndex == m_hoverSection);

    if (active || hovered) {
        QRect fRect = filterRect(rect);
        drawFilterIcon(painter, fRect, active, hovered);
    }
}

void FilterHeaderView::drawFilterIcon(QPainter *painter, const QRect &rect,
                                      bool active, bool hovered) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 颜色：激活=蓝色, 悬停=深灰, 否则=中灰
    QColor color;
    if (active)
        color = QColor(0x1A, 0x73, 0xE8);   // Google Blue
    else if (hovered)
        color = QColor(0x40, 0x40, 0x40);
    else
        color = QColor(0x90, 0x90, 0x90);

    painter->setPen(QPen(color, 1.2));
    painter->setBrush(Qt::NoBrush);

    // 绘制漏斗形状
    QPainterPath path;
    int x = rect.left();
    int y = rect.top();
    int w = rect.width();
    int h = rect.height();

    // 漏斗顶部宽，底部窄
    path.moveTo(x + 2, y + 2);
    path.lineTo(x + w - 2, y + 2);
    path.lineTo(x + w / 2 + 2, y + h / 2);
    path.lineTo(x + w / 2 + 2, y + h - 2);
    path.lineTo(x + w / 2 - 2, y + h - 2);
    path.lineTo(x + w / 2 - 2, y + h / 2);
    path.closeSubpath();

    if (active) {
        // 激活时用浅色填充
        QColor fill = color;
        fill.setAlpha(40);
        painter->setBrush(fill);
    }
    painter->drawPath(path);

    // 激活时在右下角画一个小点表示有过滤
    if (active) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(rect.right() - 2, rect.bottom() - 2, 3, 3);
    }

    painter->restore();
}

// ============================================================
//  鼠标事件
// ============================================================

void FilterHeaderView::mouseMoveEvent(QMouseEvent *event)
{
    int newHover = sectionAtFilter(event->position().toPoint());
    if (newHover < 0) {
        // 不在漏斗上，但仍需跟踪悬停列号用于显示
        int visual = visualIndexAt(static_cast<int>(event->position().x()));
        newHover = (visual >= 0) ? logicalIndex(visual) : -1;
    }

    if (newHover != m_hoverSection) {
        // 通知旧列和新列重绘
        int old = m_hoverSection;
        m_hoverSection = newHover;
        if (old >= 0)
            updateSection(old);
        if (m_hoverSection >= 0)
            updateSection(m_hoverSection);
    }

    // 设置鼠标形状：在漏斗上为手型
    int filterCol = sectionAtFilter(event->position().toPoint());
    if (filterCol >= 0)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();

    QHeaderView::mouseMoveEvent(event);
}

void FilterHeaderView::leaveEvent(QEvent *event)
{
    if (m_hoverSection >= 0) {
        int old = m_hoverSection;
        m_hoverSection = -1;
        updateSection(old);
    }
    unsetCursor();
    QHeaderView::leaveEvent(event);
}

void FilterHeaderView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int col = sectionAtFilter(event->position().toPoint());
        if (col >= 0) {
            emit filterClicked(col);
            return;  // 不传递给基类（避免触发排序）
        }
    }
    QHeaderView::mousePressEvent(event);
}
