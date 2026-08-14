#include "measurementsetupview.h"
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QGraphicsRectItem>
#include <QGraphicsTextItem>
#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPainter>
#include <QLinearGradient>
#include <QPen>
#include <QBrush>
#include <QFileInfo>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFont>
#include <cmath>
#include <QFileDialog>
#include <QListWidget>
#include <QDialog>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QGroupBox>
#include <QRadioButton>

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

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        painter->setRenderHint(QPainter::Antialiasing);
        QRectF r = rect();

        // 圆角路径
        QPainterPath path;
        path.addRoundedRect(r, 8, 8);

        // 背景渐变
        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        if (m_active) {
            grad.setColorAt(0, m_color.lighter(115));
            grad.setColorAt(1, m_color);
        } else {
            grad.setColorAt(0, QColor(0xee, 0xee, 0xee));
            grad.setColorAt(1, QColor(0xd4, 0xd4, 0xd4));
        }
        painter->fillPath(path, QBrush(grad));

        // 边框
        painter->setPen(QPen(m_active ? m_color.darker(140) : QColor(0xb8, 0xb8, 0xb8), 1.2));
        painter->drawPath(path);

        // ---- 头部区域 (固定高度 60) ----
        QRectF headerRect(r.left(), r.top(), r.width(), 60);

        // 图标
        QFont iconFont("Segoe UI Emoji", m_isSource ? 18 : 15);
        painter->setFont(iconFont);
        painter->setPen(m_active ? Qt::white : QColor(0x88, 0x88, 0x88));
        painter->drawText(QRectF(headerRect.left() + 8, headerRect.top(), 34, headerRect.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, m_icon);

        // 标题
        QFont titleFont("Microsoft YaHei UI", m_isSource ? 10 : 9, QFont::Bold);
        painter->setFont(titleFont);
        painter->setPen(m_active ? Qt::white : QColor(0x55, 0x55, 0x55));
        painter->drawText(QRectF(headerRect.left() + 42, headerRect.top() + 4,
                                 headerRect.width() - 50, headerRect.height() / 2),
                          Qt::AlignVCenter | Qt::AlignLeft, m_title);

        // 副标题 / 状态
        QFont subFont("Microsoft YaHei UI", 8);
        painter->setFont(subFont);
        painter->setPen(m_active ? QColor(255, 255, 255, 200) : QColor(0x99, 0x99, 0x99));
        QString sub;
        if (m_isSource)
            sub = m_active ? "已激活" : "未激活";
        else if (!m_instances.isEmpty())
            sub = QStringLiteral("%1 个实例").arg(m_instances.size());
        else
            sub = m_active ? "ON" : "OFF";
        painter->drawText(QRectF(headerRect.left() + 42, headerRect.top() + headerRect.height() / 2,
                                 headerRect.width() - 50, headerRect.height() / 2 - 4),
                          Qt::AlignVCenter | Qt::AlignLeft, sub);

        // 状态指示灯
        if (!m_isSource) {
            qreal cx = headerRect.right() - 12;
            qreal cy = headerRect.center().y();
            QColor dot = m_active ? QColor(0x00, 0xE6, 0x76) : QColor(0xc0, 0xc0, 0xc0);
            painter->setBrush(dot);
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(QPointF(cx, cy), 4.5, 4.5);
            // 外圈光晕
            if (m_active) {
                painter->setBrush(QColor(0x00, 0xE6, 0x76, 40));
                painter->drawEllipse(QPointF(cx, cy), 7, 7);
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
                    painter->setPen(QPen(m_active ? QColor(255, 255, 255, 80) : QColor(0xb0, 0xb0, 0xb0), 0.8));
                    painter->drawRoundedRect(tabRect, 3, 3);
                    // 标签文字
                    painter->setPen(m_active ? QColor(255, 255, 255, 230) : QColor(0x66, 0x66, 0x66));
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
                    painter->setPen(m_active ? QColor(255, 255, 255, 220) : QColor(0x66, 0x66, 0x66));
                    painter->drawText(QRectF(rowRect.left() + 8, rowRect.top(),
                                             rowRect.width() - 16, rowRect.height()),
                                      Qt::AlignVCenter | Qt::AlignLeft,
                                      QStringLiteral("> %1").arg(m_instances[i]));
                }
            }
        } else if (!m_isSource && m_instances.isEmpty() && m_active) {
            // 无实例提示
            QFont hintFont("Microsoft YaHei UI", 8);
            painter->setFont(hintFont);
            painter->setPen(QColor(255, 255, 255, 150));
            painter->drawText(QRectF(r.left() + 42, headerRect.bottom(),
                                     r.width() - 50, 20),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              "双击或右键添加实例");
        }
    }

private:
    QString m_icon, m_title;
    QColor m_color;
    bool m_active;
    bool m_isSource;
    bool m_horizontalLayout = false;
    QStringList m_instances;
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

#include "measurementsetupview.moc"

// ============================================================
//  MeasurementSetupView 实现
// ============================================================

MeasurementSetupView::MeasurementSetupView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    buildTopology();  // buildTopology() 内部已调用 relayoutModuleBlocks() → rebuildScene()

    m_recentFiles.clear();
}

void MeasurementSetupView::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 工具栏 ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(20, 20));

    m_startAct = m_toolbar->addAction("▶ 开始");

    m_stopAct = m_toolbar->addAction("■ 停止");
    m_stopAct->setEnabled(false);

    m_toolbar->addSeparator();

    layout->addWidget(m_toolbar);

    // ---- 画布 ----
    auto *scene = new SetupScene(this);
    m_scene = scene;
    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setBackgroundBrush(QColor(0xf5, 0xf5, 0xf5));
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setFocusPolicy(Qt::NoFocus);

    layout->addWidget(m_view);

    // ---- 信号连接 ----
    connect(m_startAct, &QAction::triggered, this, &MeasurementSetupView::onStartClicked);
    connect(m_stopAct, &QAction::triggered, this, &MeasurementSetupView::onStopClicked);

    connect(scene, &SetupScene::sceneClicked, this, &MeasurementSetupView::onSceneClicked);
    connect(scene, &SetupScene::sceneDoubleClicked, this, &MeasurementSetupView::onSceneDoubleClicked);
    connect(scene, &SetupScene::sceneRightClicked, this, &MeasurementSetupView::onSceneRightClicked);
}

QString MeasurementSetupView::activeSourceId() const
{
    return (m_source == Source::Hardware) ? "source_real" : "source_file";
}

void MeasurementSetupView::buildTopology()
{
    m_blocks.clear();

    // ---- 布局参数 (从左向右排列) ----
    const qreal bw = 220;   // 块宽
    const qreal bh = 60;    // 块高
    const qreal gapX = 60;  // 水平间距（列间距）
    const qreal gapY = 30;  // 垂直间距（同列块间距）
    const qreal startX = 40;
    qreal x = startX;

    // ---- 第 1 列: 数据源 (Real / File 两个块 + 切换开关) ----
    qreal srcW = 160;
    qreal srcH = bh + 10;
    qreal switchW = 70;
    qreal switchH = 32;
    qreal srcY1 = 50;
    qreal srcY2 = srcY1 + srcH + switchH + 10;

    // Real (硬件实时)
    BlockItem srcReal;
    srcReal.id = "source_real";
    srcReal.title = "Real 实时";
    srcReal.icon = "";
    srcReal.category = "source";
    srcReal.moduleName = "real";
    srcReal.rect = QRectF(x, srcY1, srcW, srcH);
    srcReal.color = QColor(0x4a, 0x90, 0xd9);
    srcReal.enabled = (m_source == Source::Hardware);
    m_blocks["source_real"] = srcReal;

    // 离线分析（文件数据源）
    BlockItem srcFile;
    srcFile.id = "source_file";
    srcFile.title = QStringLiteral("离线分析");
    srcFile.icon = "";
    srcFile.category = "source";
    srcFile.moduleName = "file";
    srcFile.rect = QRectF(x, srcY2, srcW, srcH);
    srcFile.color = QColor(0x4C, 0xAF, 0x50);
    srcFile.enabled = (m_source == Source::File);
    m_blocks["source_file"] = srcFile;

    // 切换开关位置 (在两个数据源块之间)
    m_switchRect = QRectF(x + (srcW - switchW) / 2, srcY1 + srcH + 5, switchW, switchH);

    x += srcW + gapX;

    // ---- 第 2 列: 通道 ----
    qreal chY1 = srcY1 + 5;
    qreal chY2 = srcY2 + 5;
    qreal chW = bw - 30;

    BlockItem ch1;
    ch1.id = "channel1";
    ch1.title = "CAN 通道 1";
    ch1.icon = "";
    ch1.category = "channel";
    ch1.rect = QRectF(x, chY1, chW, bh);
    ch1.color = QColor(0x00, 0x79, 0x8C);
    m_blocks["channel1"] = ch1;

    BlockItem ch2;
    ch2.id = "channel2";
    ch2.title = "CAN 通道 2";
    ch2.icon = "";
    ch2.category = "channel";
    ch2.rect = QRectF(x, chY2, chW, bh);
    ch2.color = QColor(0x00, 0x79, 0x8C);
    m_blocks["channel2"] = ch2;

    x += chW + gapX;

    // ---- 第 3 列: DBC 数据库 ----
    qreal dbY = (chY1 + chY2 + bh) / 2 - bh / 2;  // 垂直居中

    BlockItem dbc;
    dbc.id = "database";
    dbc.title = "DBC 数据库";
    dbc.icon = "";
    dbc.category = "database";
    dbc.rect = QRectF(x, dbY, bw, bh);
    dbc.color = QColor(0x7B, 0x1F, 0xA2);
    m_blocks["database"] = dbc;

    x += bw + gapX;

    // ---- 第 4 列: 分析模块 (垂直堆叠: Trace / Graphic / Data / Record) ----
    struct ModDef { QString id; QString icon; QString title; QColor color; QString moduleName; };
    ModDef mods[] = {
        {"trace1",   "", "Trace1",          QColor(0x21, 0x96, 0xF3), "trace"},
        {"graphic1", "", "Graphic1",        QColor(0xF4, 0x43, 0x36), "graphic"},
        {"data",     "", "Data 统计",        QColor(0x4C, 0xAF, 0x50), ""},
        {"record",   "", "录制 Record",      QColor(0xFF, 0x98, 0x00), ""},
    };
    int modW = 140;
    int modGap = 16;
    qreal modY = 30;

    for (int i = 0; i < 4; ++i) {
        BlockItem b;
        b.id = mods[i].id;
        b.title = mods[i].title;
        b.icon = mods[i].icon;
        b.category = "module";
        b.moduleName = mods[i].moduleName;
        b.rect = QRectF(x, modY + i * (bh + modGap), modW, bh);
        b.color = mods[i].color;
        m_blocks[mods[i].id] = b;
    }

    // ---- 连线定义 ----
    m_connections.clear();
    auto addConn = [this](const QString &from, const QString &to) {
        Connection c;
        c.fromId = from;
        c.toId = to;
        c.pathItem = nullptr;
        m_connections.append(c);
    };
    // 数据源 → 通道
    addConn("source_real", "channel1");
    addConn("source_real", "channel2");
    addConn("source_file", "channel1");
    addConn("source_file", "channel2");
    // 通道 → DBC
    addConn("channel1", "database");
    addConn("channel2", "database");
    // DBC → 各模块
    addConn("database", "trace1");
    addConn("database", "graphic1");
    addConn("database", "data");
    addConn("database", "record");

    // 模块块垂直堆叠
    relayoutModuleBlocks();
}

void MeasurementSetupView::rebuildScene()
{
    m_scene->clear();

    // 更新模块块高度 (根据实例数量动态调整)
    const qreal baseH = 60;
    const qreal rowH = 22;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        if (b.category == "module") {
            // Trace / Graphic 块独占一行，无子实例；其他模块根据实例数量动态调整高度
            if (b.moduleName != "trace" && b.moduleName != "graphic") {
                qreal h = baseH;
                if (!b.instances.isEmpty())
                    h = baseH + b.instances.size() * rowH;
                b.rect.setHeight(h);

                for (int i = 0; i < b.instances.size(); ++i) {
                    b.instances[i].subRect = QRectF(
                        b.rect.left() + 2, b.rect.top() + baseH + i * rowH,
                        b.rect.width() - 4, rowH);
                }
            }
        }
    }

    // 绘制连线
    updateConnections();

    // 绘制块
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        bool active = b.enabled;
        // 通道块始终启用 — 逻辑通道概念，不区分硬件/文件数据源
        if (b.category == "source") {
            bool isReal = (b.id == "source_real");
            b.color = isReal ? QColor(0x4a, 0x90, 0xd9) : QColor(0x4C, 0xAF, 0x50);
            if (!isReal)
                b.title = QStringLiteral("离线分析");
            // 活跃数据源高亮，非活跃灰显
            active = (b.id == activeSourceId());
        }

        auto *item = new SetupBlockGfx(b.rect, b.icon, b.title, b.color, active,
                                        b.category == "source");
        // 传递实例列表给渲染图元
        if (b.category == "module" && b.moduleName != "trace" && b.moduleName != "graphic") {
            QStringList instTitles;
            for (const auto &inst : b.instances)
                instTitles << inst.title;
            item->setInstances(instTitles);
        }
        m_scene->addItem(item);
        b.gfxItem = item;
    }

    // 绘制数据源切换开关
    if (!m_switchRect.isNull()) {
        auto *switchItem = new SourceSwitchGfx(m_switchRect, m_source == Source::Hardware);
        m_scene->addItem(switchItem);
    }

    // 更新场景矩形以适应所有块 + 开关
    QRectF sceneRect;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it)
        sceneRect = sceneRect.united(it.value().rect);
    sceneRect = sceneRect.united(m_switchRect);
    if (!sceneRect.isNull())
        m_scene->setSceneRect(sceneRect.adjusted(-20, -20, 40, 20));
}

void MeasurementSetupView::updateConnections()
{
    for (auto &conn : m_connections) {
        auto fromIt = m_blocks.find(conn.fromId);
        auto toIt = m_blocks.find(conn.toId);
        if (fromIt == m_blocks.end() || toIt == m_blocks.end())
            continue;

        QRectF from = fromIt->rect;
        QRectF to = toIt->rect;

        // 水平连线：从 from 右侧到 to 左侧
        QPointF start = QPointF(from.right(), from.center().y());
        QPointF end = QPointF(to.left(), to.center().y());

        // 如果起始块在目标块右侧，反向连接
        if (from.left() > to.right()) {
            start = QPointF(from.left(), from.center().y());
            end = QPointF(to.right(), to.center().y());
        }

        // 检查两端块是否启用
        bool fromActive = fromIt->enabled;
        bool toActive = toIt->enabled;
        if (fromIt->category == "source")
            fromActive = (fromIt->id == activeSourceId());

        // 非 active 数据源的连线不绘制（避免 4 条线交叉）
        if (fromIt->category == "source" && !fromActive)
            continue;

        // 绘制路径
        QPainterPath path;
        path.moveTo(start);

        qreal midX = (start.x() + end.x()) / 2;
        if (qAbs(start.y() - end.y()) < 2) {
            // 同一水平线上 — 直线
            path.lineTo(end);
        } else {
            // Z 型路径: 右 → 垂直 → 右
            path.lineTo(QPointF(midX, start.y()));
            path.lineTo(QPointF(midX, end.y()));
            path.lineTo(end);
        }

        auto *pathItem = new QGraphicsPathItem();
        pathItem->setPath(path);
        QColor lineColor = (fromActive && toActive) ? QColor(0xa0, 0xa0, 0xa0) : QColor(0xd0, 0xd0, 0xd0);
        Qt::PenStyle style = (fromActive && toActive) ? Qt::SolidLine : Qt::DashLine;
        pathItem->setPen(QPen(lineColor, 1.8, style, Qt::RoundCap, Qt::RoundJoin));

        m_scene->addItem(pathItem);
        conn.pathItem = pathItem;

        // 箭头 (指向右侧)
        qreal arrowSize = 6;
        QPointF arrowP1 = QPointF(end.x() - arrowSize, end.y() - arrowSize);
        QPointF arrowP2 = QPointF(end.x() - arrowSize, end.y() + arrowSize);

        QPainterPath arrowPath;
        arrowPath.moveTo(end);
        arrowPath.lineTo(arrowP1);
        arrowPath.lineTo(arrowP2);
        arrowPath.closeSubpath();

        auto *arrowItem = new QGraphicsPathItem();
        arrowItem->setPath(arrowPath);
        arrowItem->setBrush(lineColor);
        arrowItem->setPen(QPen(lineColor, 1));
        m_scene->addItem(arrowItem);
    }
}

void MeasurementSetupView::updateBlockVisual(const QString &id)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;

    auto &b = it.value();
    if (b.gfxItem) {
        auto *gfx = dynamic_cast<SetupBlockGfx*>(b.gfxItem);
        if (gfx) {
            bool active = b.enabled;
            // 通道块始终启用
            if (b.category == "source") {
                if (b.id == "source_real") {
                    gfx->setTitle(QStringLiteral("Real 实时"));
                } else {
                    gfx->setTitle(QStringLiteral("离线分析"));
                }
                active = (b.id == activeSourceId());
            }
            gfx->setActive(active);
        }
    }
}

MeasurementSetupView::BlockItem *MeasurementSetupView::blockAt(const QPointF &scenePos)
{
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        if (it.value().rect.contains(scenePos))
            return &it.value();
    }
    return nullptr;
}

bool MeasurementSetupView::instanceAt(const QPointF &scenePos, QString &moduleId, QString &instanceId)
{
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        if (b.category != "module") continue;
        for (const auto &inst : b.instances) {
            if (inst.subRect.contains(scenePos)) {
                moduleId = b.id;
                instanceId = inst.id;
                return true;
            }
        }
    }
    return false;
}

void MeasurementSetupView::toggleBlock(const QString &id)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;
    auto &b = it.value();
    if (b.category == "source") return; // 数据源不可禁用

    b.enabled = !b.enabled;
    rebuildScene();
    emit moduleToggled(b.id, b.moduleName, b.enabled);
}

// ============================================================
//  动态增删方法
// ============================================================

void MeasurementSetupView::addModuleInstance(const QString &moduleName, const QString &instanceId, const QString &title)
{
    if (m_blocks.contains(instanceId))
        return;  // 已存在

    if (moduleName == "trace" || moduleName == "graphic") {
        // Trace / Graphic: 每个实例独占一个块，分行水平排列
        BlockItem b;
        b.id = instanceId;
        b.title = title;
        if (moduleName == "trace") {
            b.icon = "";
            b.color = QColor(0x21, 0x96, 0xF3);
        } else {
            b.icon = "";
            b.color = QColor(0xF4, 0x43, 0x36);
        }
        b.category = "module";
        b.moduleName = moduleName;
        // 临时位置，relayoutModuleBlocks 会重新计算
        b.rect = QRectF(50, 300, 140, 60);
        m_blocks[instanceId] = b;

        // 添加 DBC → 模块块 的连线
        Connection c;
        c.fromId = "database";
        c.toId = instanceId;
        c.pathItem = nullptr;
        m_connections.append(c);

        relayoutModuleBlocks();
    } else {
        // 其他模块: 在现有块内添加实例
        auto it = m_blocks.find(moduleName);
        if (it == m_blocks.end()) return;
        auto &b = it.value();
        for (const auto &inst : b.instances) {
            if (inst.id == instanceId) return;  // 已存在
        }
        InstanceItem item;
        item.id = instanceId;
        item.title = title;
        b.instances.append(item);
        rebuildScene();
    }
}

void MeasurementSetupView::removeModuleInstance(const QString &moduleName, const QString &instanceId)
{
    if (moduleName == "trace" || moduleName == "graphic") {
        // Trace / Graphic: 删除整个块
        if (!m_blocks.contains(instanceId)) return;
        m_blocks.remove(instanceId);
        for (int i = m_connections.size() - 1; i >= 0; --i) {
            if (m_connections[i].fromId == instanceId || m_connections[i].toId == instanceId)
                m_connections.removeAt(i);
        }
        relayoutModuleBlocks();
    } else {
        // 其他模块: 从块内删除实例
        auto it = m_blocks.find(moduleName);
        if (it == m_blocks.end()) return;
        auto &b = it.value();
        for (int i = 0; i < b.instances.size(); ++i) {
            if (b.instances[i].id == instanceId) {
                b.instances.removeAt(i);
                rebuildScene();
                return;
            }
        }
    }
}

void MeasurementSetupView::clearTraceGraphicInstances()
{
    // 收集所有 Trace/Graphic 实例块 ID（以 "trace" 或 "graphic" 开头的块）
    QStringList toRemove;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        const auto &b = it.value();
        if (b.category == "module" &&
            (b.moduleName == "trace" || b.moduleName == "graphic"))
            toRemove << it.key();
    }
    for (const auto &id : toRemove) {
        m_blocks.remove(id);
        for (int i = m_connections.size() - 1; i >= 0; --i) {
            if (m_connections[i].fromId == id || m_connections[i].toId == id)
                m_connections.removeAt(i);
        }
    }
    if (!toRemove.isEmpty())
        relayoutModuleBlocks();
}

bool MeasurementSetupView::isBlockEnabled(const QString &blockId) const
{
    auto it = m_blocks.find(blockId);
    if (it == m_blocks.end())
        return true;  // 不存在则默认启用
    return it->enabled;
}

void MeasurementSetupView::addChannelBlock()
{
    QString id = QString("channel%1").arg(m_nextChannelNum++);
    int chNum = m_nextChannelNum - 1;

    auto refIt = m_blocks.find("channel1");
    qreal chX = refIt != m_blocks.end() ? refIt->rect.left() : 260;
    qreal chW = refIt != m_blocks.end() ? refIt->rect.width() : 190;
    qreal chH = refIt != m_blocks.end() ? refIt->rect.height() : 60;
    qreal gapY = 30;  // 垂直间距

    // 在通道列最下方添加新通道
    qreal maxY = 0;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        if (it.value().category == "channel")
            maxY = qMax(maxY, it.value().rect.bottom());
    }
    if (maxY == 0) maxY = 85;

    BlockItem ch;
    ch.id = id;
    ch.title = QString("CAN 通道 %1").arg(chNum);
    ch.icon = "";
    ch.category = "channel";
    ch.color = QColor(0x00, 0x79, 0x8C);
    ch.rect = QRectF(chX, maxY + gapY, chW, chH);
    m_blocks[id] = ch;

    Connection c1;
    c1.fromId = "source_real";
    c1.toId = id;
    c1.pathItem = nullptr;
    m_connections.append(c1);

    Connection c1b;
    c1b.fromId = "source_file";
    c1b.toId = id;
    c1b.pathItem = nullptr;
    m_connections.append(c1b);

    Connection c2;
    c2.fromId = id;
    c2.toId = "database";
    c2.pathItem = nullptr;
    m_connections.append(c2);

    rebuildScene();
}

void MeasurementSetupView::removeChannelBlock(const QString &blockId)
{
    int channelCount = 0;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        if (it.value().category == "channel")
            channelCount++;
    }
    if (channelCount <= 1) return;

    m_blocks.remove(blockId);

    for (int i = m_connections.size() - 1; i >= 0; --i) {
        if (m_connections[i].fromId == blockId || m_connections[i].toId == blockId)
            m_connections.removeAt(i);
    }

    rebuildScene();
}

void MeasurementSetupView::removeModuleBlock(const QString &blockId)
{
    m_blocks.remove(blockId);

    for (int i = m_connections.size() - 1; i >= 0; --i) {
        if (m_connections[i].fromId == blockId || m_connections[i].toId == blockId)
            m_connections.removeAt(i);
    }

    relayoutModuleBlocks();
}

void MeasurementSetupView::relayoutModuleBlocks()
{
    // 收集各类型模块块，按 ID 排序
    QStringList traceIds, graphicIds, otherIds;
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        const auto &b = it.value();
        if (b.category != "module") continue;
        if (b.moduleName == "trace")
            traceIds << it.key();
        else if (b.moduleName == "graphic")
            graphicIds << it.key();
        else
            otherIds << it.key();
    }
    traceIds.sort();
    graphicIds.sort();

    // 模块块在 DBC 块右侧垂直堆叠
    const qreal moduleH = 60;
    const qreal modW = 140;
    const qreal modGap = 16;       // 垂直间距
    qreal moduleX = 0;
    qreal moduleY = 30;
    {
        auto dbIt = m_blocks.find("database");
        if (dbIt != m_blocks.end())
            moduleX = dbIt->rect.right() + 60;  // DBC 右侧 + gapX
        else
            moduleX = 750;  // fallback
    }

    // 合并所有模块 ID，按顺序排列：Trace → Graphic → Other
    QStringList allIds;
    allIds << traceIds << graphicIds << otherIds;

    qreal y = moduleY;
    for (int i = 0; i < allIds.size(); ++i) {
        auto it = m_blocks.find(allIds[i]);
        if (it != m_blocks.end()) {
            it.value().rect = QRectF(moduleX, y, modW, moduleH);
            y += moduleH + modGap;
        }
    }

    rebuildScene();
}

void MeasurementSetupView::onSceneClicked(const QPointF &scenePos)
{
    // 检查是否点击了切换开关
    if (m_switchRect.contains(scenePos)) {
        Source newSrc = (m_source == Source::Hardware) ? Source::File : Source::Hardware;
        setSource(newSrc);
        emit sourceChanged(static_cast<int>(newSrc));
        return;
    }

    QString moduleId, instanceId;
    if (instanceAt(scenePos, moduleId, instanceId)) {
        emit moduleOpened(moduleId, instanceId);
        return;
    }

    auto *b = blockAt(scenePos);
    if (!b) return;

    if (b->category == "source") {
        if (b->id == "source_real") {
            // 点击 Real 块: 切换到 Hardware 源并跳转设备连接界面
            if (m_source != Source::Hardware) {
                setSource(Source::Hardware);
                emit sourceChanged(static_cast<int>(Source::Hardware));
            }
            emit realBlockClicked();
        } else {
            // 点击 File 块: 切换到 File 源
            if (m_source != Source::File) {
                setSource(Source::File);
                emit sourceChanged(static_cast<int>(Source::File));
            }
        }
    } else {
        // Trace / Graphic / 其他模块块: 单击切换使能/禁用
        toggleBlock(b->id);
    }
}

void MeasurementSetupView::onSceneDoubleClicked(const QPointF &scenePos)
{
    auto *b = blockAt(scenePos);
    if (!b) return;

    if (b->category == "module") {
        if (b->moduleName == "trace") {
            // Trace 块: 跳转到对应的 Trace 标签页
            emit moduleOpened("trace", b->id);
        } else if (b->moduleName == "graphic") {
            // Graphic 块: 跳转到对应的 Graphic 标签页
            emit moduleOpened("graphic", b->id);
        } else {
            emit moduleOpened(b->id, "");
        }
    } else if (b->category == "source") {
        // 双击数据源块 → 跳转对应配置界面
        if (b->id == "source_real")
            emit realBlockClicked();
        else
            emit fileBlockClicked();
    } else if (b->category == "channel") {
        // 双击通道 → 配置过滤条件
        showChannelFilterDialog(b->id);
    } else if (b->category == "database") {
        // 双击数据库 → 选择 DBC 文件
        showDbcSelectDialog();
    }
}

void MeasurementSetupView::setSource(Source src)
{
    m_source = src;
    // 更新数据源块的 enabled 状态
    if (m_blocks.contains("source_real"))
        m_blocks["source_real"].enabled = (src == Source::Hardware);
    if (m_blocks.contains("source_file"))
        m_blocks["source_file"].enabled = (src == Source::File);
    // 更新连线: 将旧数据源的连线替换为新数据源
    QString oldId = (src == Source::Hardware) ? "source_file" : "source_real";
    QString newId = (src == Source::Hardware) ? "source_real" : "source_file";
    for (auto &conn : m_connections) {
        if (conn.fromId == oldId)
            conn.fromId = newId;
    }
    rebuildScene();
}

void MeasurementSetupView::setFilePath(const QString &path)
{
    m_filePath = path;
    rebuildScene();
}

void MeasurementSetupView::onFrame(const CanFrame &)
{
    // 帧数统计由 MainWindow 状态栏统一显示，此处无需处理
}

void MeasurementSetupView::onStartClicked()
{
    m_running = true;
    m_startAct->setEnabled(false);
    m_stopAct->setEnabled(true);
    emit measurementToggled(true);
}

void MeasurementSetupView::onStopClicked()
{
    m_running = false;
    m_startAct->setEnabled(true);
    m_stopAct->setEnabled(false);
    emit measurementToggled(false);
}

void MeasurementSetupView::onBrowseClicked()
{
    emit fileBrowseRequested();
}

// ============================================================
//  右键菜单实现
// ============================================================

void MeasurementSetupView::onSceneRightClicked(const QPointF &scenePos)
{
    // 检查是否右键了实例子项
    QString instModuleId, instInstanceId;
    if (instanceAt(scenePos, instModuleId, instInstanceId)) {
        if (!m_rightMenu)
            m_rightMenu = new QMenu(this);
        else
            m_rightMenu->clear();

        auto *titleAct = m_rightMenu->addAction(QString::fromUtf8("【 %1 】").arg(instInstanceId));
        titleAct->setEnabled(false);
        QFont titleFont = titleAct->font();
        titleFont.setBold(true);
        titleAct->setFont(titleFont);
        m_rightMenu->addSeparator();

        auto *actJump = m_rightMenu->addAction("🔗 跳转到此标签页");
        connect(actJump, &QAction::triggered, this, [this, instModuleId, instInstanceId]() {
            emit moduleOpened(instModuleId, instInstanceId);
        });

        auto *actClose = m_rightMenu->addAction("❌ 删除此实例");
        connect(actClose, &QAction::triggered, this, [this, instModuleId, instInstanceId]() {
            emit moduleInstanceClosed(instModuleId, instInstanceId);
        });
    } else {
        auto *b = blockAt(scenePos);
        if (b) {
            buildContextMenu(b, scenePos);
        } else {
            buildEmptyAreaMenu(scenePos);
        }
    }

    // 将场景坐标→视图坐标→全局屏幕坐标
    QPoint globalPos = m_view->mapToGlobal(m_view->mapFromScene(scenePos));
    m_rightMenu->exec(globalPos);
}

void MeasurementSetupView::buildContextMenu(BlockItem *block, const QPointF &)
{
    if (!block) return;

    if (!m_rightMenu)
        m_rightMenu = new QMenu(this);
    else
        m_rightMenu->clear();

    // 按值拷贝关键属性，避免 lambda 捕获裸指针在块被删除后成为悬空指针
    const QString blockId = block->id;
    const QString blockTitle = block->title;
    const QString blockCategory = block->category;
    const QString blockModule = block->moduleName;
    const bool blockEnabled = block->enabled;

    // -- 标题动作（不可点）--
    auto *titleAct = m_rightMenu->addAction(QString("【 %1 】").arg(blockTitle));
    titleAct->setEnabled(false);
    QFont titleFont = titleAct->font();
    titleFont.setBold(true);
    titleAct->setFont(titleFont);
    m_rightMenu->addSeparator();

    // ---- 数据源块 ----
    if (blockCategory == "source") {
        // 切换数据源
        if (blockId == "source_real") {
            auto *actSwitch = m_rightMenu->addAction(QStringLiteral("切换到离线分析"));
            actSwitch->setStatusTip(QStringLiteral("切换到离线分析模式"));
            connect(actSwitch, &QAction::triggered, this, [this]() {
                setSource(Source::File);
                emit sourceChanged(static_cast<int>(Source::File));
            });
        } else {
            auto *actSwitch = m_rightMenu->addAction(QStringLiteral(" 切换到 Real 实时采集"));
            actSwitch->setStatusTip(QStringLiteral("切换到硬件实时采集模式"));
            connect(actSwitch, &QAction::triggered, this, [this]() {
                setSource(Source::Hardware);
                emit sourceChanged(static_cast<int>(Source::Hardware));
            });
        }

        m_rightMenu->addSeparator();
        auto *actCfg = m_rightMenu->addAction(
            blockId == "source_real" ? QStringLiteral("设备参数配置...")
                                       : QStringLiteral("打开离线分析..."));
        connect(actCfg, &QAction::triggered, this, [this, blockId]() {
            if (blockId == "source_real")
                emit realBlockClicked();
            else
                emit fileBlockClicked();
        });
    }

    // ---- 通道块 ----
    else if (blockCategory == "channel") {
        auto *actFilter = m_rightMenu->addAction("配置过滤条件...");
        actFilter->setStatusTip("设置 CAN ID 范围、扩展帧、CAN FD 等过滤参数");
        connect(actFilter, &QAction::triggered, this, [this, blockId]() {
            showChannelFilterDialog(blockId);
        });

        m_rightMenu->addSeparator();

        auto *actToggle = m_rightMenu->addAction(blockEnabled ? "⛔ 禁用通道" : "✅ 启用通道");
        connect(actToggle, &QAction::triggered, this, [this, blockId]() {
            toggleBlock(blockId);
        });

        m_rightMenu->addSeparator();

        auto *actAddCh = m_rightMenu->addAction("+ 添加通道");
        connect(actAddCh, &QAction::triggered, this, [this]() {
            addChannelBlock();
        });

        auto *actDelCh = m_rightMenu->addAction("- 删除此通道");
        int channelCount = 0;
        for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
            if (it.value().category == "channel") channelCount++;
        }
        actDelCh->setEnabled(channelCount > 1);
        connect(actDelCh, &QAction::triggered, this, [this, blockId]() {
            removeChannelBlock(blockId);
        });
    }

    // ---- 数据库块 ----
    else if (blockCategory == "database") {
        auto *actDbc = m_rightMenu->addAction("选择 DBC 文件...");
        actDbc->setStatusTip("在当前工程已加载的 DBC 文件中选择");
        connect(actDbc, &QAction::triggered, this, [this]() {
            showDbcSelectDialog();
        });

        m_rightMenu->addSeparator();

        // 显示已加载的 DBC 文件列表，每项可点击移除
        if (!m_dbcFiles.isEmpty()) {
            auto *dbcListAct = m_rightMenu->addAction(QString("已加载 DBC: %1 个").arg(m_dbcFiles.size()));
            dbcListAct->setEnabled(false);
            m_rightMenu->addSeparator();
            for (const auto &name : m_dbcFiles) {
                auto *act = m_rightMenu->addAction(QString("移除  %1").arg(name));
                act->setStatusTip("从工程中卸载此 DBC 文件");
                connect(act, &QAction::triggered, this, [this, name]() {
                    emit dbcRemoveRequested(name);
                });
            }
        } else {
            auto *noDbc = m_rightMenu->addAction("（未加载任何 DBC 文件）");
            noDbc->setEnabled(false);
        }
    }

    // ---- 模块块 ----
    else if (blockCategory == "module") {
        // 模块类型对应的添加实例动作
        if (blockModule == "trace") {
            // Trace 独立块: 跳转 + 删除（不提供添加，添加在空白区菜单）
            auto *actAdd = m_rightMenu->addAction("+ 添加 Trace 视图");
            actAdd->setStatusTip("新建一个 Trace 报文列表块");
            connect(actAdd, &QAction::triggered, this, [this]() {
                emit moduleOpened("trace", "");
            });
        } else if (blockModule == "graphic") {
            auto *actAdd = m_rightMenu->addAction("添加 Graphic 波形");
            actAdd->setStatusTip("新建一个 Graphic 波形图标签页");
            connect(actAdd, &QAction::triggered, this, [this]() {
                emit moduleOpened("graphic", "");
            });
        } else if (blockId == "data") {
            auto *actCfg = m_rightMenu->addAction("配置统计参数...");
            actCfg->setStatusTip("配置总线负载率、报文频率等统计项");
            connect(actCfg, &QAction::triggered, this, []() {
                // 占位：实际实现需要 Data 模块视图
            });
        } else if (blockId == "record") {
            auto *actCfg = m_rightMenu->addAction(" 配置录制参数...");
            actCfg->setStatusTip("设置录制文件路径和格式");
            connect(actCfg, &QAction::triggered, this, [this]() {
                emit moduleOpened("record", "");
            });
        }

        m_rightMenu->addSeparator();

        // Trace 块: 跳转到对应标签页; 其他模块: 跳转（新建）
        auto *actOpen = m_rightMenu->addAction("🔗 跳转到对应标签页");
        actOpen->setStatusTip("在中心区域打开/切换到该模块的标签页");
        connect(actOpen, &QAction::triggered, this, [this, blockModule, blockId]() {
            if (blockModule == "trace" || blockModule == "graphic")
                emit moduleOpened(blockModule, blockId);
            else
                emit moduleOpened(blockId, "");
        });

        m_rightMenu->addSeparator();

        auto *actToggle = m_rightMenu->addAction(blockEnabled ? "⛔ 禁用模块" : "✅ 启用模块");
        connect(actToggle, &QAction::triggered, this, [this, blockId]() {
            toggleBlock(blockId);
        });

        m_rightMenu->addSeparator();

        // Trace / Graphic 块: 删除实例; 其他模块: 删除块
        auto *actDelMod = m_rightMenu->addAction(
            blockModule == "trace"   ? "删除此 Trace" :
            blockModule == "graphic" ? "删除此 Graphic" :
                                              "删除此模块块");
        connect(actDelMod, &QAction::triggered, this, [this, blockModule, blockId]() {
            if (blockModule == "trace" || blockModule == "graphic")
                emit moduleInstanceClosed(blockModule, blockId);
            else
                removeModuleBlock(blockId);
        });
    }
}

void MeasurementSetupView::buildEmptyAreaMenu(const QPointF &)
{
    if (!m_rightMenu)
        m_rightMenu = new QMenu(this);
    else
        m_rightMenu->clear();

    auto *titleAct = m_rightMenu->addAction(QString::fromUtf8("【 添加配置块 】"));
    titleAct->setEnabled(false);
    QFont titleFont = titleAct->font();
    titleFont.setBold(true);
    titleAct->setFont(titleFont);
    m_rightMenu->addSeparator();

    auto *actAddCh = m_rightMenu->addAction(" 添加 CAN 通道");
    connect(actAddCh, &QAction::triggered, this, [this]() {
        addChannelBlock();
    });

    // Trace: 总是可以添加新块
    auto *actAddTrace = m_rightMenu->addAction("添加 Trace 视图");
    connect(actAddTrace, &QAction::triggered, this, [this]() {
        emit moduleOpened("trace", "");
    });

    // Graphic: 总是可以添加新块
    auto *actAddGraphic = m_rightMenu->addAction("添加 Graphic 波形");
    connect(actAddGraphic, &QAction::triggered, this, [this]() {
        emit moduleOpened("graphic", "");
    });

    // 仅在画布上不存在该类型模块时显示添加选项
    struct ModDef { QString id; QString icon; QString title; QColor color; };
    ModDef stdMods[] = {
        {"data",     "", "Data 统计",        QColor(0x4C, 0xAF, 0x50)},
        {"record",   "", "录制 Record",      QColor(0xFF, 0x98, 0x00)},
    };

    for (const auto &mod : stdMods) {
        if (!m_blocks.contains(mod.id)) {
            auto *actAdd = m_rightMenu->addAction(QString("添加 %1").arg(mod.title));
            connect(actAdd, &QAction::triggered, this, [this, mod]() {
                const qreal modW = 140;
                const qreal modH = 60;
                const qreal modGap = 16;
                // 在模块区域最下方垂直堆叠
                qreal maxY = 0;
                qreal modX = 0;
                for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
                    if (it.value().category == "module") {
                        maxY = qMax(maxY, it.value().rect.bottom());
                        modX = it.value().rect.left();
                    }
                }
                if (maxY == 0) {
                    auto dbIt = m_blocks.find("database");
                    modX = (dbIt != m_blocks.end()) ? dbIt->rect.right() + 60 : 750;
                    maxY = 30;
                }

                BlockItem b;
                b.id = mod.id;
                b.title = mod.title;
                b.icon = mod.icon;
                b.category = "module";
                b.color = mod.color;
                b.rect = QRectF(modX, maxY + modGap, modW, modH);
                m_blocks[mod.id] = b;

                Connection c;
                c.fromId = "database";
                c.toId = mod.id;
                c.pathItem = nullptr;
                m_connections.append(c);

                rebuildScene();
            });
        }
    }
}

// ============================================================
//  File 文件回放配置对话框 — 仅选择文件 + 文件列表
// ============================================================
void MeasurementSetupView::showFileConfigDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("File 文件回放配置");
    dlg.setMinimumWidth(420);
    auto *lay = new QVBoxLayout(&dlg);

    // 当前文件
    auto *curLabel = new QLabel(
        m_filePath.isEmpty() ? QStringLiteral("当前未选择文件")
                               : QStringLiteral("当前: %1").arg(m_filePath),
        &dlg);
    curLabel->setWordWrap(true);
    lay->addWidget(curLabel);

    // 文件列表
    auto *fileHint = new QLabel("最近文件:", &dlg);
    lay->addWidget(fileHint);
    auto *fileList = new QListWidget(&dlg);
    fileList->setMinimumHeight(120);
    fileList->setMaximumHeight(180);
    for (const auto &f : m_recentFiles)
        fileList->addItem(f);
    if (m_recentFiles.isEmpty())
        fileList->addItem("（暂无最近文件，请点击“浏览...”选择）");
    lay->addWidget(fileList);

    // 浏览按钮
    auto *browseBtn = new QPushButton(" 浏览其他文件...", &dlg);
    lay->addWidget(browseBtn);

    // 浏览文件
    QObject::connect(browseBtn, &QPushButton::clicked, this, [this, &dlg]() {
        emit fileBrowseRequested();
        dlg.accept();
    });

    // 双击文件列表
    QObject::connect(fileList, &QListWidget::itemDoubleClicked, this, [this, fileList](QListWidgetItem *) {
        int row = fileList->currentRow();
        if (row >= 0 && row < m_recentFiles.size()) {
            setFilePath(m_recentFiles[row]);
            emit fileBrowseRequested();
        }
    });

    // 按钮组
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, this, [this, fileList, &dlg]() {
        int row = fileList->currentRow();
        if (row >= 0 && row < m_recentFiles.size()) {
            setFilePath(m_recentFiles[row]);
            emit fileBrowseRequested();
        }
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

// ============================================================
//  通道过滤条件配置对话框
// ============================================================
// NOTE: Real 硬件参数配置已移至设备连接界面 (DeviceConnectionTab)
//       点击 Flow 页面的 Real 块将跳转到设备连接标签页
void MeasurementSetupView::showChannelFilterDialog(const QString &channelId)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QString("配置 %1 过滤条件").arg(channelId));
    dlg.setMinimumWidth(380);
    auto *form = new QFormLayout(&dlg);

    // CAN ID 范围
    auto *idMin = new QSpinBox(&dlg);
    idMin->setRange(0, 0x7FF);
    idMin->setDisplayIntegerBase(16);
    idMin->setPrefix("0x");
    idMin->setValue(0);

    auto *idMax = new QSpinBox(&dlg);
    idMax->setRange(0, 0x7FF);
    idMax->setDisplayIntegerBase(16);
    idMax->setPrefix("0x");
    idMax->setValue(0x7FF);

    auto *idRangeWidget = new QWidget(&dlg);
    auto *idRangeLay = new QHBoxLayout(idRangeWidget);
    idRangeLay->setContentsMargins(0, 0, 0, 0);
    idRangeLay->addWidget(idMin);
    idRangeLay->addWidget(new QLabel("~", idRangeWidget));
    idRangeLay->addWidget(idMax);
    form->addRow("ID 范围:", idRangeWidget);

    // 帧类型选项
    auto *chkStd   = new QCheckBox("标准帧 (11-bit ID)", &dlg);
    auto *chkExt   = new QCheckBox("扩展帧 (29-bit ID)", &dlg);
    auto *chkFD    = new QCheckBox("CAN FD 帧", &dlg);
    auto *chkRTR   = new QCheckBox("RTR 远程帧", &dlg);
    chkStd->setChecked(true);
    chkExt->setChecked(true);

    auto *frameTypes = new QGroupBox("帧类型过滤", &dlg);
    auto *ftLay = new QVBoxLayout(frameTypes);
    ftLay->addWidget(chkStd);
    ftLay->addWidget(chkExt);
    ftLay->addWidget(chkFD);
    ftLay->addWidget(chkRTR);
    form->addRow(frameTypes);

    // 方向过滤
    auto *comboDir = new QComboBox(&dlg);
    comboDir->addItems({"全部", "仅 Tx (发送)", "仅 Rx (接收)"});
    form->addRow("方向:", comboDir);

    // 按钮
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(btns);

    connect(btns, &QDialogButtonBox::accepted, this, [this, channelId, idMin, idMax, chkStd, chkExt, chkFD, chkRTR, comboDir, &dlg]() {
        // 将过滤配置信息输出到底部输出栏
        QStringList filterDesc;
        filterDesc << QString("%1: ID 0x%2~0x%3")
                      .arg(channelId)
                      .arg(idMin->value(), 0, 16)
                      .arg(idMax->value(), 0, 16);
        QStringList types;
        if (chkStd->isChecked()) types << "Std";
        if (chkExt->isChecked()) types << "Ext";
        if (chkFD->isChecked())  types << "FD";
        if (chkRTR->isChecked()) types << "RTR";
        filterDesc << QString("帧类型: %1").arg(types.join(", "));
        filterDesc << QString("方向: %1").arg(comboDir->currentText());
        qDebug() << "Channel filter configured:" << filterDesc;
        // 通知 MainWindow
        emit channelFilterRequested(channelId);
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

// ============================================================
//  DBC 文件选择对话框
// ============================================================
void MeasurementSetupView::showDbcSelectDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("选择 DBC 文件");
    dlg.setMinimumWidth(420);
    auto *lay = new QVBoxLayout(&dlg);

    auto *hint = new QLabel("当前工程已加载的 DBC 文件:", &dlg);
    lay->addWidget(hint);

    auto *list = new QListWidget(&dlg);
    list->setAlternatingRowColors(true);
    list->setMinimumHeight(150);
    for (const auto &name : m_dbcFiles)
        list->addItem(name);
    if (m_dbcFiles.isEmpty())
        list->addItem("（未加载任何 DBC 文件，请先通过侧边栏导入）");
    lay->addWidget(list);

    // 导入新文件按钮
    auto *importBtn = new QPushButton("导入新 DBC 文件...", &dlg);
    lay->addWidget(importBtn);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);

    // 双击选择
    connect(list, &QListWidget::itemDoubleClicked, this, [this, list, &dlg](QListWidgetItem *) {
        int row = list->currentRow();
        if (row >= 0 && row < m_dbcFiles.size()) {
            qDebug() << "Selected DBC:" << m_dbcFiles[row];
            emit dbcSelectRequested();
            dlg.accept();
        }
    });

    connect(importBtn, &QPushButton::clicked, this, [this, &dlg]() {
        emit dbcSelectRequested();
        dlg.accept();
    });

    connect(btns, &QDialogButtonBox::accepted, this, [this, list, &dlg]() {
        int row = list->currentRow();
        if (row >= 0 && row < m_dbcFiles.size()) {
            qDebug() << "Selected DBC:" << m_dbcFiles[row];
        }
        emit dbcSelectRequested();
        dlg.accept();
    });
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

