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
#include <QRectF>
#include <QGuiApplication>
#include <QScreen>
#include <QtGlobal>

/**
 * Monochrome SVG icons — VS Code Codicons look (16px grid, currentColor tint).
 *
 * Size tokens (match layout toggles / explorer trees):
 *   kIconMd = 16  — default toolbar / chrome
 *   kIconSm = 14  — compact rows, clear buttons, command-center nav
 * Avoid rendering below 14px: glyphs look soft and low-contrast on HiDPI.
 */

inline constexpr int kIconSm = 14;
inline constexpr int kIconMd = 16;
inline constexpr int kIconLg = 20;

// Load SVG, replace currentColor, render crisp at device pixel ratio.
inline QPixmap renderSvgPixmap(const QString &resourcePath, const QString &color, int size = kIconMd)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QString svg = QString::fromUtf8(file.readAll());
    svg.replace(QStringLiteral("currentColor"), color);
    QSvgRenderer renderer(svg.toUtf8());
    if (!renderer.isValid())
        return {};

    qreal dpr = 1.0;
    if (QScreen *screen = QGuiApplication::primaryScreen())
        dpr = screen->devicePixelRatio();
    if (dpr < 1.0)
        dpr = 1.0;

    const int px = qMax(1, qRound(static_cast<qreal>(size) * dpr));
    QPixmap pixmap(px, px);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(0, 0, px, px));
    return pixmap;
}

// Convenience wrapper: monochrome QIcon
inline QIcon svgIcon(const QString &resourcePath, const QString &color, int size = kIconMd)
{
    return QIcon(renderSvgPixmap(resourcePath, color, size));
}

// Native QLineEdit clear-button glyph is easy to miss — use themed close.svg
inline void applyClearButtonIcon(QLineEdit *edit, const QString &color)
{
    const auto buttons = edit->findChildren<QToolButton *>();
    for (auto *btn : buttons) {
        btn->setIcon(svgIcon(":/icons/close.svg", color, kIconSm));
        btn->setIconSize(QSize(kIconSm, kIconSm));
    }
}

/// Leading search glyph + themed clear button (VS Code explorer filter).
inline void applyExplorerSearch(QLineEdit *edit, const QString &iconColor, const QString &dimColor)
{
    if (!edit)
        return;
    edit->setClearButtonEnabled(true);
    applyClearButtonIcon(edit, iconColor);
    edit->addAction(svgIcon(QStringLiteral(":/icons/search.svg"), dimColor, kIconSm),
                    QLineEdit::LeadingPosition);
}

/// VS Code Explorer tree: compact rows, indent guides on hover/select (via
/// GrayBranchStyle), full-row selection.
inline void applyExplorerTree(QTreeWidget *tree,
                              const QString &objectName = QStringLiteral("ExplorerTree"))
{
    if (!tree)
        return;
    if (!objectName.isEmpty())
        tree->setObjectName(objectName);
    tree->setRootIsDecorated(true);
    tree->setIndentation(12);
    tree->setIconSize(QSize(kIconMd, kIconMd));
    tree->setAlternatingRowColors(false);
    tree->setUniformRowHeights(true);
    tree->setAnimated(false);
    tree->setItemsExpandable(true);
    tree->setAllColumnsShowFocus(true);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    tree->setFocusPolicy(Qt::StrongFocus);
    tree->setMouseTracking(true);
    if (tree->viewport())
        tree->viewport()->setMouseTracking(true);
    QPalette pal = tree->palette();
    const QColor guide(0xA0, 0xA0, 0xA0);
    pal.setColor(QPalette::Mid, guide);
    pal.setColor(QPalette::Dark, guide);
    tree->setPalette(pal);
}

#endif // OPENBUS_SVG_ICON_H
