#ifndef OPENBUS_GRAY_BRANCH_STYLE_H
#define OPENBUS_GRAY_BRANCH_STYLE_H

#include <QProxyStyle>
#include <QPainter>
#include <QStyleOption>
#include <QStyleFactory>
#include <QApplication>
#include <QAbstractItemView>

/**
 * VS Code Explorer indent guides (renderIndentGuides: onHover):
 * - Chevrons always
 * - Vertical guides only when the row is hovered or selected
 *
 * IMPORTANT: Do not put any QTreeView::branch rules in QSS — any branch
 * subcontrol rule makes QStyleSheetStyle own PE_IndicatorBranch and fall back
 * to the native selected (accent/blue) bars, skipping this proxy.
 */
class GrayBranchStyle : public QProxyStyle
{
public:
    explicit GrayBranchStyle(QStyle *base)
        : QProxyStyle(base)
    {
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override
    {
        if (element == PE_IndicatorBranch) {
            paintGrayBranch(option, painter);
            return; // never call native — avoids thick Highlight-colored bars
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void polish(QWidget *widget) override
    {
        QProxyStyle::polish(widget);
        if (auto *view = qobject_cast<QAbstractItemView *>(widget)) {
            view->setMouseTracking(true);
            if (view->viewport())
                view->viewport()->setMouseTracking(true);
        }
    }

    /// Call before setStyleSheet so QStyleSheetStyle wraps this proxy.
    static void installOnApp()
    {
        qApp->setStyleSheet(QString()); // drop old sheet so setStyle is not ignored
        QStyle *base = QStyleFactory::create(QStringLiteral("Fusion"));
        if (!base)
            base = QStyleFactory::create(QStringLiteral("windows"));
        qApp->setStyle(new GrayBranchStyle(base));
    }

private:
    static void paintGrayBranch(const QStyleOption *option, QPainter *painter)
    {
        if (!option || !painter)
            return;

        const QRect r = option->rect;
        if (!r.isValid())
            return;

        const QColor guide(0x8A, 0x8A, 0x8A);
        const QColor chevron(0x6C, 0x6C, 0x6C);
        const int x = r.x() + r.width() / 2;
        const int cy = r.y() + r.height() / 2;

        const bool hasChildren = option->state & State_Children;
        const bool isOpen = option->state & State_Open;
        const bool hasSibling = option->state & State_Sibling;
        const bool hasItem = option->state & State_Item;
        const bool showGuides = (option->state & State_MouseOver)
            || (option->state & State_Selected);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);

        if (showGuides && (hasSibling || hasItem)) {
            painter->setPen(QPen(guide, 1));
            if (hasSibling)
                painter->drawLine(x, r.top(), x, r.bottom());
            else
                painter->drawLine(x, r.top(), x, cy);
        }

        if (hasChildren) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(chevron, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (isOpen) {
                painter->drawLine(x - 3, cy - 1, x, cy + 2);
                painter->drawLine(x, cy + 2, x + 3, cy - 1);
            } else {
                painter->drawLine(x - 1, cy - 3, x + 2, cy);
                painter->drawLine(x + 2, cy, x - 1, cy + 3);
            }
        }

        painter->restore();
    }
};

#endif // OPENBUS_GRAY_BRANCH_STYLE_H
