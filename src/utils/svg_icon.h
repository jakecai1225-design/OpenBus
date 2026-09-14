#ifndef OPENBUS_SVG_ICON_H
#define OPENBUS_SVG_ICON_H

#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QPixmap>
#include <QLineEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QAbstractItemView>
#include <QSize>

// Load SVG, replace currentColor, render to QPixmap
inline QPixmap renderSvgPixmap(const QString &resourcePath, const QString &color, int size = 22)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString svg = QString::fromUtf8(file.readAll());
    svg.replace(QStringLiteral("currentColor"), color);
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    return pixmap;
}

// Convenience wrapper: monochrome QIcon
inline QIcon svgIcon(const QString &resourcePath, const QString &color, int size = 16)
{
    return QIcon(renderSvgPixmap(resourcePath, color, size));
}

// Native QLineEdit clear-button glyph is easy to miss — use themed close.svg
inline void applyClearButtonIcon(QLineEdit *edit, const QString &color)
{
    const auto buttons = edit->findChildren<QToolButton *>();
    for (auto *btn : buttons)
        btn->setIcon(svgIcon(":/icons/close.svg", color, 12));
}

/// Leading search glyph + themed clear button (VS Code explorer filter).
inline void applyExplorerSearch(QLineEdit *edit, const QString &iconColor, const QString &dimColor)
{
    if (!edit)
        return;
    edit->setClearButtonEnabled(true);
    applyClearButtonIcon(edit, iconColor);
    edit->addAction(svgIcon(QStringLiteral(":/icons/search.svg"), dimColor, 14),
                    QLineEdit::LeadingPosition);
}

/// VS Code Explorer tree: compact rows, indent guides + chevrons (via QSS),
/// full-row selection including the branch column.
inline void applyExplorerTree(QTreeWidget *tree,
                              const QString &objectName = QStringLiteral("ExplorerTree"))
{
    if (!tree)
        return;
    if (!objectName.isEmpty())
        tree->setObjectName(objectName);
    tree->setRootIsDecorated(true);
    tree->setIndentation(12);
    tree->setIconSize(QSize(16, 16));
    tree->setAlternatingRowColors(false);
    tree->setUniformRowHeights(true);
    tree->setAnimated(false);
    tree->setItemsExpandable(true);
    tree->setAllColumnsShowFocus(true);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->setFocusPolicy(Qt::StrongFocus);
}

#endif // OPENBUS_SVG_ICON_H
