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

        // 图标
        QFont iconFont("Segoe UI Emoji", m_isSource ? 18 : 15);
        painter->setFont(iconFont);
        painter->setPen(m_active ? Qt::white : QColor(0x88, 0x88, 0x88));
        painter->drawText(QRectF(r.left() + 8, r.top(), 34, r.height()),
                          Qt::AlignVCenter | Qt::AlignLeft, m_icon);

        // 标题
        QFont titleFont("Microsoft YaHei UI", m_isSource ? 10 : 9, QFont::Bold);
        painter->setFont(titleFont);
        painter->setPen(m_active ? Qt::white : QColor(0x55, 0x55, 0x55));
        painter->drawText(QRectF(r.left() + 42, r.top() + 4, r.width() - 50, r.height() / 2),
                          Qt::AlignVCenter | Qt::AlignLeft, m_title);

        // 副标题 / 状态
        QFont subFont("Microsoft YaHei UI", 8);
        painter->setFont(subFont);
        painter->setPen(m_active ? QColor(255, 255, 255, 200) : QColor(0x99, 0x99, 0x99));
        QString sub = m_active ? (m_isSource ? "已激活" : "ON") : (m_isSource ? "" : "OFF");
        painter->drawText(QRectF(r.left() + 42, r.top() + r.height() / 2,
                                 r.width() - 50, r.height() / 2 - 4),
                          Qt::AlignVCenter | Qt::AlignLeft, sub);

        // 状态指示灯
        if (!m_isSource) {
            qreal cx = r.right() - 12;
            qreal cy = r.center().y();
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
    }

private:
    QString m_icon, m_title;
    QColor m_color;
    bool m_active;
    bool m_isSource;
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

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            emit sceneClicked(event->scenePos());
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
    buildTopology();
    rebuildScene();
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

    auto *srcLabel = new QLabel("  数据源: ", m_toolbar);
    m_toolbar->addWidget(srcLabel);

    m_hwBtn = new QToolButton(m_toolbar);
    m_hwBtn->setText("🔧 硬件实时");
    m_hwBtn->setCheckable(true);
    m_hwBtn->setAutoRaise(true);
    m_hwBtn->setStyleSheet("QToolButton { padding: 2px 8px; font-size: 9pt; }"
                           "QToolButton:checked { background: #4a90d9; color: white; border-radius: 3px; }");
    m_toolbar->addWidget(m_hwBtn);

    m_fileBtn = new QToolButton(m_toolbar);
    m_fileBtn->setText("📁 文件回放");
    m_fileBtn->setCheckable(true);
    m_fileBtn->setAutoRaise(true);
    m_fileBtn->setStyleSheet("QToolButton { padding: 2px 8px; font-size: 9pt; }"
                             "QToolButton:checked { background: #4CAF50; color: white; border-radius: 3px; }");
    m_toolbar->addWidget(m_fileBtn);

    m_hwBtn->setChecked(true);

    m_toolbar->addSeparator();

    m_browseAct = m_toolbar->addAction("📂 选择文件");
    m_browseAct->setVisible(false);

    m_toolbar->addSeparator();

    m_startAct = m_toolbar->addAction("▶ 开始测量");

    m_stopAct = m_toolbar->addAction("■ 停止");
    m_stopAct->setEnabled(false);

    m_toolbar->addSeparator();

    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);

    m_statusLabel = new QLabel("帧数: 0", m_toolbar);
    m_statusLabel->setStyleSheet("padding: 0 10px; color: #666;");
    m_toolbar->addWidget(m_statusLabel);

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
    connect(m_hwBtn, &QToolButton::clicked, this, [this]() {
        setSource(Source::Hardware);
        emit sourceChanged(static_cast<int>(Source::Hardware));
    });
    connect(m_fileBtn, &QToolButton::clicked, this, [this]() {
        setSource(Source::File);
        emit sourceChanged(static_cast<int>(Source::File));
    });
    connect(m_browseAct, &QAction::triggered, this, &MeasurementSetupView::onBrowseClicked);
    connect(m_startAct, &QAction::triggered, this, &MeasurementSetupView::onStartClicked);
    connect(m_stopAct, &QAction::triggered, this, &MeasurementSetupView::onStopClicked);

    connect(scene, &SetupScene::sceneClicked, this, &MeasurementSetupView::onSceneClicked);
    connect(scene, &SetupScene::sceneDoubleClicked, this, &MeasurementSetupView::onSceneDoubleClicked);
}

void MeasurementSetupView::buildTopology()
{
    m_blocks.clear();

    // ---- 布局参数 ----
    const qreal bw = 220;   // 块宽
    const qreal bh = 60;    // 块高
    const qreal gapX = 40;  // 水平间距
    const qreal gapY = 55;  // 垂直间距
    const qreal startX = 50;
    qreal y = 30;

    // ---- 第 1 行: 数据源 ----
    BlockItem src;
    src.id = "source";
    src.title = (m_source == Source::Hardware) ? "硬件实时采集" : "文件回放分析";
    src.icon = (m_source == Source::Hardware) ? "\xF0\x9F\x94\xA7" : "\xF0\x9F\x93\x81";
    src.category = "source";
    src.rect = QRectF(startX + 100, y, bw, bh + 10);
    src.color = (m_source == Source::Hardware) ? QColor(0x4a, 0x90, 0xd9) : QColor(0x4C, 0xAF, 0x50);
    src.enabled = true;
    m_blocks["source"] = src;
    y += src.rect.height() + gapY;

    // ---- 第 2 行: 通道 / 文件源 ----
    BlockItem ch1;
    ch1.id = "channel1";
    ch1.title = "CAN 通道 1";
    ch1.icon = "\xF0\x9F\x93\xA1";
    ch1.category = "channel";
    ch1.rect = QRectF(startX, y, bw - 30, bh);
    ch1.color = QColor(0x00, 0x79, 0x8C);
    m_blocks["channel1"] = ch1;

    BlockItem ch2;
    ch2.id = "channel2";
    ch2.title = "CAN 通道 2";
    ch2.icon = "\xF0\x9F\x93\xA1";
    ch2.category = "channel";
    ch2.rect = QRectF(startX + bw - 30 + gapX, y, bw - 30, bh);
    ch2.color = QColor(0x00, 0x79, 0x8C);
    m_blocks["channel2"] = ch2;
    y += bh + gapY;

    // ---- 第 3 行: DBC 数据库 ----
    BlockItem dbc;
    dbc.id = "database";
    dbc.title = "DBC 数据库";
    dbc.icon = "\xF0\x9F\x93\x84";
    dbc.category = "database";
    dbc.rect = QRectF(startX + 100, y, bw, bh);
    dbc.color = QColor(0x7B, 0x1F, 0xA2);
    m_blocks["database"] = dbc;
    y += bh + gapY;

    // ---- 第 4 行: 分析模块（4列）----
    struct ModDef { QString id; QString icon; QString title; QColor color; };
    ModDef mods[] = {
        {"trace",    "\xF0\x9F\x93\x8B", "Trace 报文列表",   QColor(0x21, 0x96, 0xF3)},
        {"graphic",  "\xF0\x9F\x93\x88", "Graphic 波形",     QColor(0xF4, 0x43, 0x36)},
        {"data",     "\xF0\x9F\x93\x8A", "Data 统计",        QColor(0x4C, 0xAF, 0x50)},
        {"record",   "\xE2\x97\x8F",     "录制 Record",      QColor(0xFF, 0x98, 0x00)},
    };
    int modW = 160;
    int modGap = 20;
    int totalW = 4 * modW + 3 * modGap;
    int modStartX = startX + 100 + (bw - totalW) / 2;
    if (modStartX < 20) modStartX = 20;

    for (int i = 0; i < 4; ++i) {
        BlockItem b;
        b.id = mods[i].id;
        b.title = mods[i].title;
        b.icon = mods[i].icon;
        b.category = "module";
        b.rect = QRectF(modStartX + i * (modW + modGap), y, modW, bh);
        b.color = mods[i].color;
        m_blocks[mods[i].id] = b;
    }
    y += bh + gapY;

    // ---- 连线定义 ----
    m_connections.clear();
    auto addConn = [this](const QString &from, const QString &to) {
        Connection c;
        c.fromId = from;
        c.toId = to;
        c.pathItem = nullptr;
        m_connections.append(c);
    };
    // 数据源 → 通道1, 通道2
    addConn("source", "channel1");
    addConn("source", "channel2");
    // 通道 → DBC
    addConn("channel1", "database");
    addConn("channel2", "database");
    // DBC → 各模块
    addConn("database", "trace");
    addConn("database", "graphic");
    addConn("database", "data");
    addConn("database", "record");

    // 设置场景大小
    m_scene->setSceneRect(0, 0, modStartX + totalW + 30, y + 20);
}

void MeasurementSetupView::rebuildScene()
{
    m_scene->clear();

    // 绘制连线
    updateConnections();

    // 绘制块
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto &b = it.value();
        bool active = b.enabled;
        if (b.category == "channel" && m_source == Source::File) {
            // 文件模式下通道块显示为禁用
            active = false;
        }
        if (b.category == "source") {
            b.title = (m_source == Source::Hardware) ? "硬件实时采集" : "文件回放分析";
            b.icon = (m_source == Source::Hardware) ? "\xF0\x9F\x94\xA7" : "\xF0\x9F\x93\x81";
            b.color = (m_source == Source::Hardware) ? QColor(0x4a, 0x90, 0xd9) : QColor(0x4C, 0xAF, 0x50);
            active = true;
        }

        auto *item = new SetupBlockGfx(b.rect, b.icon, b.title, b.color, active,
                                        b.category == "source");
        m_scene->addItem(item);
        b.gfxItem = item;
    }
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

        QPointF start = QPointF(from.center().x(), from.bottom());
        QPointF end = QPointF(to.center().x(), to.top());

        // 如果起始块在目标块下方，调整起止点
        if (from.top() > to.bottom()) {
            start = QPointF(from.center().x(), from.top());
            end = QPointF(to.center().x(), to.bottom());
        }

        // 如果不在同一垂直线上，画 L 型路径
        QPainterPath path;
        path.moveTo(start);

        qreal midY = (start.y() + end.y()) / 2;
        if (qAbs(start.x() - end.x()) < 2) {
            // 垂直线
            path.lineTo(end);
        } else {
            // L 型路径: 下 → 水平 → 下
            path.lineTo(QPointF(start.x(), midY));
            path.lineTo(QPointF(end.x(), midY));
            path.lineTo(end);
        }

        // 绘制路径
        auto *pathItem = new QGraphicsPathItem();
        pathItem->setPath(path);
        pathItem->setPen(QPen(QColor(0xa0, 0xa0, 0xa0), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        // 检查两端块是否启用
        bool fromActive = fromIt->enabled;
        bool toActive = toIt->enabled;
        if (fromIt->category == "source") fromActive = true;
        if (toIt->category == "channel" && m_source == Source::File) toActive = false;

        if (!fromActive || !toActive) {
            pathItem->setPen(QPen(QColor(0xd0, 0xd0, 0xd0), 1.5, Qt::DashLine));
        }

        m_scene->addItem(pathItem);
        conn.pathItem = pathItem;

        // 箭头
        qreal arrowSize = 6;
        qreal angle = std::atan2(end.y() - (end.y() - arrowSize), 0); // 向下箭头
        QPointF arrowP1 = QPointF(end.x() - arrowSize, end.y() - arrowSize);
        QPointF arrowP2 = QPointF(end.x() + arrowSize, end.y() - arrowSize);

        QPainterPath arrowPath;
        arrowPath.moveTo(end);
        arrowPath.lineTo(arrowP1);
        arrowPath.lineTo(arrowP2);
        arrowPath.closeSubpath();

        auto *arrowItem = new QGraphicsPathItem();
        arrowItem->setPath(arrowPath);
        QColor arrowColor = (fromActive && toActive) ? QColor(0xa0, 0xa0, 0xa0) : QColor(0xd0, 0xd0, 0xd0);
        arrowItem->setBrush(arrowColor);
        arrowItem->setPen(QPen(arrowColor, 1));
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
            if (b.category == "channel" && m_source == Source::File)
                active = false;
            if (b.category == "source") {
                gfx->setTitle((m_source == Source::Hardware) ? "硬件实时采集" : "文件回放分析");
                active = true;
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

void MeasurementSetupView::toggleBlock(const QString &id)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;
    auto &b = it.value();
    if (b.category == "source") return; // 数据源不可禁用

    b.enabled = !b.enabled;
    rebuildScene();
    emit moduleToggled(b.title, b.enabled);
}

void MeasurementSetupView::onSceneClicked(const QPointF &scenePos)
{
    auto *b = blockAt(scenePos);
    if (b && b->category != "source") {
        toggleBlock(b->id);
    }
}

void MeasurementSetupView::onSceneDoubleClicked(const QPointF &scenePos)
{
    auto *b = blockAt(scenePos);
    if (b) {
        if (b->category == "module")
            emit moduleOpened(b->id);
        else if (b->category == "source") {
            // 双击数据源切换
            if (m_source == Source::Hardware) {
                setSource(Source::File);
                emit sourceChanged(static_cast<int>(Source::File));
            } else {
                setSource(Source::Hardware);
                emit sourceChanged(static_cast<int>(Source::Hardware));
            }
        }
    }
}

void MeasurementSetupView::setSource(Source src)
{
    m_source = src;
    m_hwBtn->setChecked(src == Source::Hardware);
    m_fileBtn->setChecked(src == Source::File);
    m_browseAct->setVisible(src == Source::File);
    rebuildScene();
}

void MeasurementSetupView::setFilePath(const QString &path)
{
    m_filePath = path;
    rebuildScene();
}

void MeasurementSetupView::onFrame(const CanFrame &)
{
    m_frameCount++;
    if (m_frameCount % 100 == 0)
        m_statusLabel->setText(QString("帧数: %1").arg(m_frameCount));
}

void MeasurementSetupView::onStartClicked()
{
    m_running = true;
    m_startAct->setEnabled(false);
    m_stopAct->setEnabled(true);
    m_hwBtn->setEnabled(false);
    m_fileBtn->setEnabled(false);
    m_statusLabel->setText("▶ 测量运行中...");
    m_frameCount = 0;
    emit measurementToggled(true);
}

void MeasurementSetupView::onStopClicked()
{
    m_running = false;
    m_startAct->setEnabled(true);
    m_stopAct->setEnabled(false);
    m_hwBtn->setEnabled(true);
    m_fileBtn->setEnabled(true);
    m_statusLabel->setText(QString("■ 已停止 (帧数: %1)").arg(m_frameCount));
    emit measurementToggled(false);
}

void MeasurementSetupView::onBrowseClicked()
{
    emit fileBrowseRequested();
}
