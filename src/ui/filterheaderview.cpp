#include "filterheaderview.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
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
    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() { viewport()->update(); };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));
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
    const bool isSorted = (m_sortColumn == logicalIndex);
    const bool filterActive = hasFilter(logicalIndex);
    // 当前排序列 / 活跃过滤：始终显示；其余仅悬停时提示可交互
    const bool showSort = isSorted || hovered;
    const bool showFilter = filterActive || hovered;

    // 1. 基类绘制背景 + 文字（排序指示器已禁用，不会绘制）
    painter->save();
    QHeaderView::paintSection(painter, rect, logicalIndex);
    painter->restore();

    // 2. 列分隔线（右侧）— 主题色
    painter->save();
    painter->setPen(QPen(QColor(th.borderDim), 1));
    painter->drawLine(rect.right(), rect.top(), rect.right(), rect.bottom());
    painter->restore();

    if ((!showSort && !showFilter) || rect.width() < 50)
        return;

    QRect fRect = filterRect(rect);
    QRect sRect = sortIndicatorRect(rect);

    // Icon-area mask so title text does not bleed under the glyphs
    const int iconLeft = (showSort ? sRect.left() : fRect.left()) - 3;
    QRect maskRect(iconLeft, rect.top() + 1,
                   rect.right() - iconLeft + 1, rect.height() - 1);
    painter->save();
    painter->setPen(Qt::NoPen);
    // 悬停用 headerHover；仅持久指示时用表头底色遮罩文字
    painter->setBrush(QColor(hovered ? th.headerHover : th.headerBg));
    painter->drawRect(maskRect);
    painter->restore();

    if (showSort) {
        if (isSorted)
            drawSortIndicator(painter, sRect, m_sortOrder == Qt::AscendingOrder);
        else
            drawSortHint(painter, sRect);
    }

    if (showFilter)
        drawFilterIcon(painter, fRect, filterActive);
}

void FilterHeaderView::drawSortIndicator(QPainter *painter, const QRect &rect, bool ascending) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const Theme &th = ThemeManager::instance()->currentTheme();
    QColor color(th.accent);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);

    // 略内缩，三角更大更醒目
    const qreal padX = 1.0;
    const qreal padY = 1.5;
    const qreal l = rect.left() + padX;
    const qreal r = rect.right() - padX;
    const qreal t = rect.top() + padY;
    const qreal b = rect.bottom() - padY;
    const qreal cx = (l + r) / 2.0;

    QPainterPath path;
    if (ascending) {
        path.moveTo(cx, t);
        path.lineTo(r, b);
        path.lineTo(l, b);
    } else {
        path.moveTo(cx, b);
        path.lineTo(r, t);
        path.lineTo(l, t);
    }
    path.closeSubpath();
    painter->drawPath(path);

    painter->restore();
}

void FilterHeaderView::drawSortHint(QPainter *painter, const QRect &rect) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 灰化上下双箭头：明确“可排序”，比单点/单三角更易发现
    QColor color(ThemeManager::instance()->currentTheme().textDim);
    color.setAlpha(200);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);

    const qreal cx = rect.center().x();
    const qreal w = rect.width() * 0.42;   // 半宽
    const qreal gap = 1.0;                 // 上下三角间距
    const qreal h = (rect.height() - gap) / 2.0 - 0.5;

    // ▲ 上半
    {
        QPainterPath up;
        const qreal top = rect.top() + 0.5;
        const qreal bot = top + h;
        up.moveTo(cx, top);
        up.lineTo(cx + w, bot);
        up.lineTo(cx - w, bot);
        up.closeSubpath();
        painter->drawPath(up);
    }
    // ▼ 下半
    {
        QPainterPath down;
        const qreal bot = rect.bottom() - 0.5;
        const qreal top = bot - h;
        down.moveTo(cx, bot);
        down.lineTo(cx + w, top);
        down.lineTo(cx - w, top);
        down.closeSubpath();
        painter->drawPath(down);
    }

    painter->restore();
}

void FilterHeaderView::drawFilterIcon(QPainter *painter, const QRect &rect, bool active) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Caller only paints while hovered; active filter → accent
    const Theme &th = ThemeManager::instance()->currentTheme();
    QColor color = active ? QColor(th.accent) : QColor(th.text);

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
