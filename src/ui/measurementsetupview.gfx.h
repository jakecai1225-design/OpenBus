#ifndef MEASUREMENTSETUPVIEW_GFX_H
#define MEASUREMENTSETUPVIEW_GFX_H

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QStringList>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QTimer>

/**
 * @brief Block lamp states (internal to translation unit)
 * 
 * - None: disabled (no light)
 * - Idle: enabled and ready (solid green)
 * - Flow: active data stream (green blink)
 * - Error: runtime exception (red blink)
 */
enum class BlockLamp { None, Idle, Flow, Error };

// ============================================================
//  自定义图元 — 可绘制带圆角渐变和文字的块
// ============================================================

class SetupBlockGfx : public QGraphicsRectItem
{
public:
    SetupBlockGfx(const QRectF &rect, const QString &icon, const QString &title,
                  const QColor &color, bool active, bool isSource)
        : QGraphicsRectItem(rect)
        , m_icon(icon), m_title(title), m_color(color)
        , m_active(active), m_isSource(isSource)
    {
        setAcceptHoverEvents(true);
        setFlag(ItemIsSelectable, true);
    }

    void setActive(bool a) { m_active = a; update(); }
    void setTitle(const QString &t) { m_title = t; update(); }
    void setInstances(const QStringList &list) { m_instances = list; update(); }
    void setHorizontalLayout(bool h) { m_horizontalLayout = h; update(); }
    /// Filter 块模式：实例行 = 过滤规则行（副标题显示规则数）
    void setRuleMode(bool r) { m_ruleMode = r; update(); }
    /// 设置状态灯（状态 + 当前闪烁相位；由视图层定时驱动）
    void setLamp(BlockLamp s, bool blinkPhase)
    { m_lamp = s; m_blinkPhase = blinkPhase; update(); }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        QRectF r = rect();

        // 圆角路径
        QPainterPath path;
        path.addRoundedRect(r, 8, 8);

        // ✅ 背景填充（激活用蓝色渐变，非激活用白色）
        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        if (m_active) {
            grad.setColorAt(0, QColor(0x42, 0xA5, 0xF5));  // 亮蓝
            grad.setColorAt(1, QColor(0x19, 0x76, 0xD2));  // 深蓝
        } else {
            grad.setColorAt(0, Qt::white);                  // 白色
            grad.setColorAt(1, Qt::white);                  // 白色
        }
        painter->fillPath(path, QBrush(grad));

        // ✅ 边框色（激活用深蓝，非激活用灰色）
        painter->setPen(QPen(m_active ? QColor(0x0D, 0x47, 0xA6) : QColor(0xC0, 0xC0, 0xC0), 1.2));
        painter->drawPath(path);

        // ---- 头部区域 (固定高度 60) ----
        QRectF headerRect(r.left(), r.top(), r.width(), 60);

        // 图标（根据激活状态选择白色/深灰）
        QFont iconFont("Segoe UI Emoji", m_isSource ? 18 : 15);
        painter->setFont(iconFont);
        painter->setPen(m_active ? Qt::white : QColor(0x45, 0x5A, 0x6B));
        painter->drawText(QRectF(headerRect.left() + 8, headerRect.top(), 34, headerRect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, m_icon);

        // 标题（根据激活状态选择白色/深蓝灰）
        QFont titleFont("Microsoft YaHei UI", m_isSource ? 10 : 9, QFont::Bold);
        painter->setFont(titleFont);
        painter->setPen(m_active ? Qt::white : QColor(0x26, 0x32, 0x38));
        painter->drawText(QRectF(headerRect.left() + 42, headerRect.top() + 4,
                                 headerRect.width() - 50, headerRect.height() / 2),
                          Qt::AlignVCenter | Qt::AlignLeft, m_title);

        // 副标题 / 状态
        QFont subFont("Microsoft YaHei UI", 8);
        painter->setFont(subFont);
        painter->setPen(m_active ? QColor(255, 255, 255, 200) : QColor(0x54, 0x6E, 0x7A));
        QString sub;
        if (m_isSource)
            sub = m_active ? "已激活" : "未激活";
        else if (m_ruleMode)
            sub = m_instances.isEmpty()
                      ? (m_active ? "ON" : "OFF")
                      : QStringLiteral("%1 条规则").arg(m_instances.size());
        else if (!m_instances.isEmpty())
            sub = QStringLiteral("%1 个实例").arg(m_instances.size());
        else
            sub = m_active ? "ON" : "OFF";
        painter->drawText(QRectF(headerRect.left() + 42, headerRect.top() + headerRect.height() / 2,
                                 headerRect.width() - 50, headerRect.height() / 2 - 4),
                          Qt::AlignVCenter | Qt::AlignLeft, sub);

        // 状态指示灯（灯态由视图层计算：未使能不亮；待命常亮绿；
        // 数据流绿闪；异常红闪；数据源块仅异常亮灯）
        {
            qreal cx = headerRect.right() - 12;
            qreal cy = headerRect.center().y();
            QColor lampColor;
            qreal lampR = 4.5;
            bool glow = false;
            bool draw = false;
            switch (m_lamp) {
            case BlockLamp::None:
                break;
            case BlockLamp::Idle:
                lampColor = QColor(0x00, 0xE6, 0x76);
                draw = true;
                glow = true;
                break;
            case BlockLamp::Flow:
                lampColor = QColor(0x00, 0xE6, 0x76);
                draw = true;
                glow = m_blinkPhase;
                lampR = m_blinkPhase ? 4.5 : 3.0;
                if (!m_blinkPhase)
                    lampColor.setAlpha(140);
                break;
            case BlockLamp::Error:
                lampColor = QColor(0xFF, 0x45, 0x3A);
                draw = true;
                glow = m_blinkPhase;
                lampR = m_blinkPhase ? 4.5 : 3.0;
                if (!m_blinkPhase)
                    lampColor.setAlpha(140);
                break;
            }
            if (draw) {
                painter->setBrush(lampColor);
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(QPointF(cx, cy), lampR, lampR);
                if (glow) {
                    // 外圈光晕
                    QColor halo = lampColor;
                    halo.setAlpha(40);
                    painter->setBrush(halo);
                    painter->drawEllipse(QPointF(cx, cy), 7, 7);
                }
            }
        }

        // ---- 实例列表区域 (模块块) ----
        if (!m_isSource && !m_instances.isEmpty()) {
            qreal rowH = 22;
            QFont instFont("Microsoft YaHei UI", 8);

            if (m_horizontalLayout) {
                // 水平平铺布局 (Trace 模块): 实例作为水平标签
                qreal tabW = 90;
                qreal tabH = rowH;
                qreal tabY = headerRect.bottom();
                painter->setFont(instFont);
                for (int i = 0; i < m_instances.size(); ++i) {
                    QRectF tabRect(r.left() + 4 + i * tabW, tabY, tabW - 4, tabH);
                    // 标签背景
                    QColor bg = m_active ? QColor(255, 255, 255, 50) : QColor(0, 0, 0, 15);
                    painter->fillRect(tabRect, bg);
                    // 标签边框
                    painter->setPen(QPen(m_active ? QColor(255, 255, 255, 80) : QColor(0x90, 0xA5, 0xB8), 0.8));
                    painter->drawRoundedRect(tabRect, 3, 3);
                    // 标签文字
                    painter->setPen(m_active ? QColor(255, 255, 255, 230) : QColor(0x54, 0x6E, 0x7A));
                    painter->drawText(tabRect, Qt::AlignVCenter | Qt::AlignCenter, m_instances[i]);
                }
            } else {
                // 垂直列表布局 (其他模块)
                for (int i = 0; i < m_instances.size(); ++i) {
                    QRectF rowRect(r.left() + 2, headerRect.bottom() + i * rowH,
                                   r.width() - 4, rowH);

                    // 实例行背景 (交替色)
                    if (i % 2 == 0)
                        painter->fillRect(rowRect, QColor(255, 255, 255, 30));
                    else
                        painter->fillRect(rowRect, QColor(0, 0, 0, 10));

                    // 实例标题
                    painter->setFont(instFont);
                    painter->setPen(m_active ? QColor(255, 255, 255, 220) : QColor(0x54, 0x6E, 0x7A));
                    painter->drawText(QRectF(rowRect.left() + 8, rowRect.top(),
                                             rowRect.width() - 16, rowRect.height()),
                                      Qt::AlignVCenter | Qt::AlignLeft,
                                      QStringLiteral("> %1").arg(m_instances[i]));
                }
            }
        } else if (!m_isSource && m_instances.isEmpty() && m_active) {
            // Empty-instance hint (Filter: filter rules; others: open config)
            QFont hintFont("Microsoft YaHei UI", 8);
            painter->setFont(hintFont);
            painter->setPen(m_active ? QColor(255, 255, 255, 150) : QColor(0x78, 0x90, 0xA9));
            painter->drawText(QRectF(r.left() + 42, headerRect.bottom(),
                                     r.width() - 50, 20),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              m_ruleMode ? QStringLiteral("Click/double-click to configure filter")
                                         : QStringLiteral("Click/double-click to open config"));
        }
    }

private:
    QString m_icon, m_title;
    QColor m_color;
    bool m_active;
    bool m_isSource;
    bool m_horizontalLayout = false;
    bool m_ruleMode = false;
    QStringList m_instances;
    BlockLamp m_lamp = BlockLamp::None;  ///< 状态灯（默认不亮）
    bool m_blinkPhase = false;          ///< 闪烁相位（true = 亮）
};

// ============================================================
//  自定义图元 — 数据源切换开关 (Real / File)
// ============================================================

class SourceSwitchGfx : public QGraphicsRectItem
{
public:
    SourceSwitchGfx(const QRectF &rect, bool isReal)
        : QGraphicsRectItem(rect), m_isReal(isReal)
    {
        setAcceptHoverEvents(true);
    }

    void setIsReal(bool isReal) { m_isReal = isReal; update(); }

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        QRectF r = rect();

        // 开关背景 (圆角矩形)
        QPainterPath path;
        path.addRoundedRect(r, r.height() / 2, r.height() / 2);
        painter->fillPath(path, QBrush(QColor(0xe8, 0xe8, 0xe8)));
        painter->setPen(QPen(QColor(0xc0, 0xc0, 0xc0), 1));
        painter->drawPath(path);

        // 文字标签
        QFont labelFont("Microsoft YaHei UI", 7, QFont::Bold);
        painter->setFont(labelFont);

        qreal halfW = r.width() / 2;
        qreal knobR = r.height() / 2 - 3;
        QPointF knobCenter;

        if (m_isReal) {
            knobCenter = QPointF(r.left() + knobR + 3, r.center().y());
            painter->setPen(QColor(0x4a, 0x90, 0xd9));
            painter->drawText(QRectF(r.left(), r.top(), halfW, r.height()),
                              Qt::AlignCenter, "Real");
            painter->setPen(QColor(0xaa, 0xaa, 0xaa));
            painter->drawText(QRectF(r.left() + halfW, r.top(), halfW, r.height()),
                              Qt::AlignCenter, "File");
        } else {
            knobCenter = QPointF(r.right() - knobR - 3, r.center().y());
            painter->setPen(QColor(0xaa, 0xaa, 0xaa));
            painter->drawText(QRectF(r.left(), r.top(), halfW, r.height()),
                              Qt::AlignCenter, "Real");
            painter->setPen(QColor(0x4C, 0xAF, 0x50));
            painter->drawText(QRectF(r.left() + halfW, r.top(), halfW, r.height()),
                              Qt::AlignCenter, "File");
        }

        // 滑块 (knob)
        QColor knobColor = m_isReal ? QColor(0x4a, 0x90, 0xd9) : QColor(0x4C, 0xAF, 0x50);
        painter->setBrush(knobColor);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(knobCenter, knobR, knobR);

        // 滑块高光
        painter->setBrush(QColor(255, 255, 255, 60));
        painter->drawEllipse(QPointF(knobCenter.x(), knobCenter.y() - knobR * 0.3),
                            knobR * 0.55, knobR * 0.55);
    }

private:
    bool m_isReal;
};

// ============================================================
//  自定义场景 — 处理点击和双击
// ============================================================

class SetupScene : public QGraphicsScene
{
    Q_OBJECT
public:
    explicit SetupScene(QObject *parent = nullptr) : QGraphicsScene(parent) {}

signals:
    void sceneClicked(const QPointF &pos);
    void sceneDoubleClicked(const QPointF &pos);
    void sceneRightClicked(const QPointF &pos);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emit sceneClicked(event->scenePos());
        else if (event->button() == Qt::RightButton)
            emit sceneRightClicked(event->scenePos());
        QGraphicsScene::mousePressEvent(event);
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emit sceneDoubleClicked(event->scenePos());
        QGraphicsScene::mouseDoubleClickEvent(event);
    }
};

#endif // MEASUREMENTSETUPVIEW_GFX_H
