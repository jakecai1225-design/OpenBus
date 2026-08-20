#include "filterheaderview.h"
#include "thememanager.h"
#include "models/cantraceproxymodel.h"

#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPalette>

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
    // 禁用 Qt 内置排序指示器 — 自行绘制以控制位置，避免与过滤图标重叠
    setSortIndicatorShown(false);

    // 主题切换 → 重绘（图标颜色取自 ThemeManager，而非硬编码）
    // DEF-08 字符串信号：ThemeManager 定义于 data.dll，跨 DLL PMF connect 断连
connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            this, [this]() { viewport()->update(); });
}

void FilterHeaderView::setProxyModel(CanTraceProxyModel *proxy)
{
    m_proxy = proxy;
}

bool FilterHeaderView::hasFilter(int logicalIndex) const
{
    return m_proxy && m_proxy->hasColumnFilter(logicalIndex);
}

void FilterHeaderView::setSortState(int column, Qt::SortOrder order)
{
    int oldCol = m_sortColumn;
    m_sortColumn = column;
    m_sortOrder = order;
    // 重绘旧列（移除箭头）和新列（添加箭头）
    if (oldCol >= 0 && oldCol != column)
        updateSection(oldCol);
    if (column >= 0)
        updateSection(column);
}

void FilterHeaderView::clearSortState()
{
    int oldCol = m_sortColumn;
    m_sortColumn = -1;
    m_sortOrder = Qt::AscendingOrder;
    if (oldCol >= 0)
        updateSection(oldCol);
}

// ============================================================
//  图标区域计算 — 排序在左，过滤在右，互不重叠
// ============================================================

QRect FilterHeaderView::filterRect(const QRect &sectionRect) const
{
    // 漏斗图标放在列最右侧
    int x = sectionRect.right() - kFilterSize - kRightMargin;
    int y = sectionRect.top() + (sectionRect.height() - kFilterSize) / 2;
    return QRect(x, y, kFilterSize, kFilterSize);
}

QRect FilterHeaderView::sortIndicatorRect(const QRect &sectionRect) const
{
    // 排序三角形在漏斗图标左侧，保持间距
    int x = sectionRect.right() - kFilterSize - kRightMargin - kSortSize - kGap;
    int y = sectionRect.top() + (sectionRect.height() - kSortSize) / 2;
    return QRect(x, y, kSortSize, kSortSize);
}

int FilterHeaderView::sectionAtFilter(const QPoint &pos) const
{
    int visual = visualIndexAt(pos.x());
    if (visual < 0)
        return -1;
    int logical = logicalIndex(visual);
    if (logical < 0)
        return -1;
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
    if (!rect.isValid())
        return;

    const Theme &th = ThemeManager::instance()->currentTheme();
    const bool hovered = (logicalIndex == m_hoverSection);

    // 1. 基类绘制背景 + 文字（排序指示器已禁用，不会绘制）
    painter->save();
    QHeaderView::paintSection(painter, rect, logicalIndex);
    painter->restore();

    // 2. 列分隔线（右侧）— 主题色
    painter->save();
    painter->setPen(QPen(QColor(th.borderDim), 1));
    painter->drawLine(rect.right(), rect.top(), rect.right(), rect.bottom());
    painter->restore();

    if (rect.width() < 50)
        return;  // 太窄不画图标

    QRect fRect = filterRect(rect);
    QRect sRect = sortIndicatorRect(rect);

    // 3. 在图标区域绘制背景遮罩，防止文字渗入图标下方
    //    颜色取当前节背景（悬停节用 headerHover），避免遮罩色块突兀
    int iconLeft = sRect.left() - 3;
    QRect maskRect(iconLeft, rect.top() + 1,
                   rect.right() - iconLeft + 1, rect.height() - 1);
    painter->save();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(hovered ? th.headerHover : th.headerBg));
    painter->drawRect(maskRect);
    painter->restore();

    bool active = hasFilter(logicalIndex);

    // 4. 排序三角形 — 当前排序列绘制实心三角
    if (m_sortColumn == logicalIndex) {
        drawSortIndicator(painter, sRect, m_sortOrder == Qt::AscendingOrder);
    } else if (hovered) {
        // 非排序列悬停时显示提示点（主题次级色）
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QColor hint(th.textDim);
        painter->setPen(Qt::NoPen);
        painter->setBrush(hint);
        int dotSize = 3;
        int dx = sRect.left() + (sRect.width() - dotSize) / 2;
        int dy = sRect.top() + (sRect.height() - dotSize) / 2;
        painter->drawEllipse(QPointF(dx + dotSize / 2.0, dy + dotSize / 2.0),
                             dotSize / 2.0, dotSize / 2.0);
        painter->restore();
    }

    // 5. 过滤漏斗图标 — 始终可见
    drawFilterIcon(painter, fRect, active, hovered);
}

void FilterHeaderView::drawSortIndicator(QPainter *painter, const QRect &rect, bool ascending) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    QColor color(ThemeManager::instance()->currentTheme().accent);  // 主题色实心三角
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);

    QPainterPath path;
    if (ascending) {
        // 向上三角 ▲
        path.moveTo(rect.left() + rect.width() / 2.0, rect.top() + 0.5);
        path.lineTo(rect.right() - 0.5, rect.bottom() - 0.5);
        path.lineTo(rect.left() + 0.5, rect.bottom() - 0.5);
    } else {
        // 向下三角 ▼
        path.moveTo(rect.left() + rect.width() / 2.0, rect.bottom() - 0.5);
        path.lineTo(rect.right() - 0.5, rect.top() + 0.5);
        path.lineTo(rect.left() + 0.5, rect.top() + 0.5);
    }
    path.closeSubpath();
    painter->drawPath(path);

    painter->restore();
}

void FilterHeaderView::drawFilterIcon(QPainter *painter, const QRect &rect,
                                      bool active, bool hovered) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 颜色（主题色）：激活=accent, 悬停=text, 否则=textDim（始终可见）
    const Theme &th = ThemeManager::instance()->currentTheme();
    QColor color;
    if (active)
        color = QColor(th.accent);
    else if (hovered)
        color = QColor(th.text);
    else
        color = QColor(th.textDim);

    painter->setPen(QPen(color, 1.5));
    painter->setBrush(Qt::NoBrush);

    // 绘制漏斗形状
    QPainterPath path;
    int x = rect.left();
    int y = rect.top();
    int w = rect.width();
    int h = rect.height();

    path.moveTo(x + 2, y + 2);
    path.lineTo(x + w - 2, y + 2);
    path.lineTo(x + w / 2 + 2, y + h / 2);
    path.lineTo(x + w / 2 + 2, y + h - 2);
    path.lineTo(x + w / 2 - 2, y + h - 2);
    path.lineTo(x + w / 2 - 2, y + h / 2);
    path.closeSubpath();

    if (active) {
        QColor fill = color;
        fill.setAlpha(60);
        painter->setBrush(fill);
    }
    painter->drawPath(path);

    // 激活时在右下角画一个小圆点表示有过滤
    if (active) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(QPointF(rect.right() - 1, rect.bottom() - 1), 2.5, 2.5);
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
