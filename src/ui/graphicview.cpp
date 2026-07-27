#include "graphicview.h"
#include "signalconfigdialog.h"

#include <QPainter>
#include <QPainterPath>
#include <QMenu>
#include <QAction>
#include <QPaintEvent>
#include <QFontDatabase>
#include <QDateTime>
#include <cmath>
#include <algorithm>

GraphicView::GraphicView(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 150);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

QColor GraphicView::autoColor(int index)
{
    static const QColor colors[] = {
        QColor(0x21, 0x96, 0xF3), // 蓝
        QColor(0xF4, 0x43, 0x36), // 红
        QColor(0x4C, 0xAF, 0x50), // 绿
        QColor(0xFF, 0x98, 0x00), // 橙
        QColor(0x9C, 0x27, 0xB0), // 紫
        QColor(0x00, 0xBC, 0xD4), // 青
        QColor(0xFF, 0xEB, 0x3B), // 黄
        QColor(0x79, 0x55, 0x48), // 棕
    };
    return colors[index % 8];
}

void GraphicView::addSignal(const Signal &sig)
{
    SignalData sd;
    sd.config = sig;
    if (!sd.config.color.isValid())
        sd.config.color = autoColor(m_signals.size());
    m_signals.append(sd);
    update();
}

void GraphicView::removeSignal(int index)
{
    if (index >= 0 && index < m_signals.size()) {
        m_signals.removeAt(index);
        update();
    }
}

void GraphicView::clearSignals()
{
    m_signals.clear();
    update();
}

QVector<GraphicView::Signal> GraphicView::signalConfigs() const
{
    QVector<Signal> result;
    for (const auto &sd : m_signals)
        result.append(sd.config);
    return result;
}

void GraphicView::clearData()
{
    for (auto &sd : m_signals)
        sd.points.clear();
    m_currentTime = 0.0;
    update();
}

void GraphicView::onFrame(const CanFrame &frame)
{
    m_currentTime = frame.timestamp;

    for (auto &sd : m_signals) {
        if ((frame.id & 0x1FFFFFFF) == sd.config.canId &&
            frame.extended == sd.config.extended) {
            double val = extractValue(frame, sd.config);
            if (!std::isnan(val)) {
                sd.points.append({frame.timestamp, val});
                // 丢弃窗口外的旧点
                double cutoff = frame.timestamp - m_timeWindow;
                while (!sd.points.isEmpty() && sd.points.first().time < cutoff)
                    sd.points.removeFirst();
            }
        }
    }

    update();
}

double GraphicView::extractValue(const CanFrame &frame, const Signal &sig) const
{
    int needed = sig.bitLength / 8;
    if (sig.byteOffset + needed > frame.data.size())
        return std::numeric_limits<double>::quiet_NaN();

    const auto *p = reinterpret_cast<const unsigned char *>(frame.data.constData()) + sig.byteOffset;

    if (sig.bigEndian) {
        quint64 val = 0;
        for (int i = 0; i < needed; ++i)
            val = (val << 8) | p[i];
        return static_cast<double>(val);
    } else {
        quint64 val = 0;
        for (int i = 0; i < needed; ++i)
            val |= static_cast<quint64>(p[i]) << (8 * i);
        return static_cast<double>(val);
    }
}

void GraphicView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRectF rect = this->rect().adjusted(0, 0, -1, -1);

    // 背景
    p.fillRect(rect, QColor(0xFA, 0xFA, 0xFA));

    // 绘图区域留白
    qreal marginLeft = 50;
    qreal marginRight = 10;
    qreal marginTop = 10;
    qreal marginBottom = 25;
    QRectF plotArea(rect.left() + marginLeft, rect.top() + marginTop,
                    rect.width() - marginLeft - marginRight,
                    rect.height() - marginTop - marginBottom);

    // 计算 Y 范围
    double minVal = 0.0, maxVal = 255.0;
    bool hasData = false;
    for (const auto &sd : m_signals) {
        for (const auto &pt : sd.points) {
            if (!hasData) {
                minVal = maxVal = pt.value;
                hasData = true;
            } else {
                minVal = std::min(minVal, pt.value);
                maxVal = std::max(maxVal, pt.value);
            }
        }
    }
    if (hasData && minVal == maxVal) {
        minVal -= 1;
        maxVal += 1;
    }
    // 留 10% 边距
    double range = maxVal - minVal;
    minVal -= range * 0.1;
    maxVal += range * 0.1;

    drawGrid(p, plotArea);

    // 时间范围
    double tEnd = m_currentTime;
    double tStart = tEnd - m_timeWindow;

    // 绘制每个信号
    for (const auto &sd : m_signals) {
        if (sd.points.isEmpty())
            continue;

        p.setPen(QPen(sd.config.color, 1.5));
        QPainterPath path;
        bool first = true;
        for (const auto &pt : sd.points) {
            double x = plotArea.left() + (pt.time - tStart) / m_timeWindow * plotArea.width();
            double y = plotArea.bottom() - (pt.value - minVal) / (maxVal - minVal) * plotArea.height();

            if (x < plotArea.left()) {
                first = true;
                continue;
            }
            if (x > plotArea.right())
                break;

            if (first) {
                path.moveTo(x, y);
                first = false;
            } else {
                path.lineTo(x, y);
            }
        }
        p.drawPath(path);

        // 在最右侧绘制当前值
        if (!sd.points.isEmpty()) {
            const auto &last = sd.points.last();
            double x = plotArea.left() + (last.time - tStart) / m_timeWindow * plotArea.width();
            double y = plotArea.bottom() - (last.value - minVal) / (maxVal - minVal) * plotArea.height();
            if (x >= plotArea.left() && x <= plotArea.right()) {
                p.setBrush(sd.config.color);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(x, y), 3, 3);
            }
        }
    }

    // Y 轴标签
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    p.setFont(mono);
    p.setPen(QColor(0x66, 0x66, 0x66));
    for (int i = 0; i <= 5; ++i) {
        double val = minVal + (maxVal - minVal) * i / 5.0;
        double y = plotArea.bottom() - plotArea.height() * i / 5.0;
        p.drawText(QRectF(rect.left(), y - 10, marginLeft - 5, 20),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(val, 'f', 0));
    }

    // X 轴标签
    for (int i = 0; i <= 5; ++i) {
        double t = tStart + m_timeWindow * i / 5.0;
        double x = plotArea.left() + plotArea.width() * i / 5.0;
        p.drawText(QRectF(x - 40, plotArea.bottom() + 3, 80, 20),
                   Qt::AlignCenter,
                   QString::number(t, 'f', 2) + "s");
    }

    // 图例
    drawLegend(p);

    // 无信号提示
    if (m_signals.isEmpty()) {
        p.setPen(QColor(0xAA, 0xAA, 0xAA));
        QFont f = font();
        f.setPointSize(12);
        p.setFont(f);
        p.drawText(rect, Qt::AlignCenter,
                   "右键添加信号 | 选择 CAN ID + 字节偏移来绘制信号曲线");
    }
}

void GraphicView::drawGrid(QPainter &p, const QRectF &plotArea)
{
    p.setPen(QPen(QColor(0xE0, 0xE0, 0xE0), 1, Qt::DotLine));

    // 水平网格
    for (int i = 0; i <= 5; ++i) {
        double y = plotArea.top() + plotArea.height() * i / 5.0;
        p.drawLine(QPointF(plotArea.left(), y), QPointF(plotArea.right(), y));
    }

    // 垂直网格
    for (int i = 0; i <= 5; ++i) {
        double x = plotArea.left() + plotArea.width() * i / 5.0;
        p.drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
    }

    // 边框
    p.setPen(QPen(QColor(0xCC, 0xCC, 0xCC), 1));
    p.drawRect(plotArea);
}

void GraphicView::drawLegend(QPainter &p)
{
    if (m_signals.isEmpty())
        return;

    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);

    int legendW = 0;
    int legendH = m_signals.size() * 18 + 8;
    for (const auto &sd : m_signals) {
        int w = p.fontMetrics().horizontalAdvance(sd.config.name + "  ");
        legendW = std::max(legendW, w + 30);
    }

    QRectF legendRect(width() - legendW - 10, 10, legendW, legendH);
    p.setBrush(QColor(255, 255, 255, 200));
    p.setPen(QPen(QColor(0xCC, 0xCC, 0xCC), 1));
    p.drawRoundedRect(legendRect, 4, 4);

    for (int i = 0; i < m_signals.size(); ++i) {
        qreal y = legendRect.top() + 4 + i * 18 + 9;
        p.setPen(Qt::NoPen);
        p.setBrush(m_signals[i].config.color);
        p.drawRect(QRectF(legendRect.left() + 6, y - 5, 12, 10));
        p.setPen(QColor(0x33, 0x33, 0x33));
        p.drawText(QRectF(legendRect.left() + 22, y - 9, legendW - 28, 18),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   m_signals[i].config.name);
    }
}

void GraphicView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

    QAction addAction("添加信号...", this);
    QAction clearAction("清除所有信号", this);
    menu.addAction(&addAction);
    menu.addSeparator();
    menu.addAction(&clearAction);

    // 已有信号列表
    if (!m_signals.isEmpty()) {
        menu.addSeparator();
        for (int i = 0; i < m_signals.size(); ++i) {
            QAction *rmAction = menu.addAction("删除: " + m_signals[i].config.name);
            rmAction->setData(i);
        }
    }

    QAction *sel = menu.exec(event->globalPos());
    if (!sel)
        return;

    if (sel == &addAction) {
        SignalConfigDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            Signal sig;
            sig.name = dlg.signalName();
            sig.canId = dlg.canId();
            sig.extended = dlg.isExtended();
            sig.byteOffset = dlg.byteOffset();
            sig.bitLength = dlg.bitLength();
            sig.bigEndian = dlg.isBigEndian();
            addSignal(sig);
        }
    } else if (sel == &clearAction) {
        clearSignals();
    } else if (sel->data().isValid()) {
        removeSignal(sel->data().toInt());
    }
}

