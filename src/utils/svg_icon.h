#ifndef OPENBUS_SVG_ICON_H
#define OPENBUS_SVG_ICON_H

#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QPixmap>
#include <QLineEdit>
#include <QToolButton>

// 读取 SVG 文件，替换 currentColor 为指定颜色，渲染为 QPixmap
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

// 便捷封装：直接返回单色 QIcon
inline QIcon svgIcon(const QString &resourcePath, const QString &color, int size = 16)
{
    return QIcon(renderSvgPixmap(resourcePath, color, size));
}

// QLineEdit 内嵌清除按钮（setClearButtonEnabled）默认使用原生 × 图标，
// 深色主题下几乎不可见 — 换为主题色 close.svg 图标
inline void applyClearButtonIcon(QLineEdit *edit, const QString &color)
{
    const auto buttons = edit->findChildren<QToolButton *>();
    for (auto *btn : buttons)
        btn->setIcon(svgIcon(":/icons/close.svg", color, 12));
}

#endif // OPENBUS_SVG_ICON_H
