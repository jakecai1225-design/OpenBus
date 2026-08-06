#ifndef SIN_SVG_ICON_H
#define SIN_SVG_ICON_H

#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QFile>
#include <QPixmap>

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

#endif // SIN_SVG_ICON_H
