#include "measurementsetupview.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"
#include "utils/logging.h"
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
#include <QTimer>
#include <QDateTime>

// ============================================================
//  功能块状态灯语义（仅本翻译单元；用户需求 2026-08-24）：
//  未使能不亮灯（块灰化）；使能待命=常亮绿；数据流活跃=绿闪；
//  运行异常=红闪；数据源块仅异常亮红灯
// ============================================================
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
            // 无实例提示（Filter 块提示过滤配置入口）
            QFont hintFont("Microsoft YaHei UI", 8);
            painter->setFont(hintFont);
            painter->setPen(m_active ? QColor(255, 255, 255, 150) : QColor(0x78, 0x90, 0xA9));
            painter->drawText(QRectF(r.left() + 42, headerRect.bottom(),
                                     r.width() - 50, 20),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              m_ruleMode ? "单击/双击配置过滤条件"
                                         : "单击/双击打开对应标签页");
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

#include "measurementsetupview.moc"

// ============================================================
//  MeasurementSetupView 实现
// ============================================================

MeasurementSetupView::MeasurementSetupView(QWidget *parent)
    : QWidget(parent)
{
    // 灯闪烁相位驱动（先于建场景：rebuildScene 末尾会投影灯态；
    // 仅数据流活跃或存在异常时运转，平时停转避免空刷新）
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkOn = !m_blinkOn;
        updateBlockLamps();
    });

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

    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_startAct = m_toolbar->addAction(svgIcon(":/icons/play.svg", iconCol, 20), "开始");

    m_stopAct = m_toolbar->addAction(svgIcon(":/icons/stop.svg", iconCol, 20), "停止");
    m_stopAct->setEnabled(false);

    m_toolbar->addSeparator();

    layout->addWidget(m_toolbar);

    // ---- 画布 ----
    auto *scene = new SetupScene(this);
    m_scene = scene;
    m_view = new QGraphicsView(m_scene, this);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setBackgroundBrush(QColor(0xff, 0xff, 0xff));
    m_view->setFrameShape(QFrame::NoFrame);
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
    // 考虑信号发生器作为可选数据源
    if (m_source == Source::Hardware) 
        return "source_real";
    else if (m_source == Source::File)
        return "source_file";
    return QString();  // 没有活跃数据源时返回空字符串
}

void MeasurementSetupView::buildTopology()
{
    m_blocks.clear();

    // ---- 布局参数 (从左向右排列) ----
    const qreal bw = 220;   // 块宽
    const qreal bh = 60;    // 块高（统一高度）
    const qreal gapX = 60;  // 水平间距（列间距）
    const qreal startX = 40;
    
    // ---- 数据源块相关参数（统一高度 bh，不使用额外高度） ----
    const qreal srcW = 160;
    const qreal srcH = bh;  // ✅ 与主流程块等高
    const qreal switchW = 70;
    const qreal switchH = 32;
    
    // ---- 第 1 列：CAN 信号发生器 (唯一数据源头)
    BlockItem signalGen;
    signalGen.id = "signal_generator";
    signalGen.title = "信号发生器";  // 移除"CAN"前缀
    signalGen.icon = "";
    signalGen.category = "source";
    signalGen.moduleName = "signal_generator";
    signalGen.rect = QRectF(startX, 50, srcW, srcH);  // ✅ 使用统一高度 srcH=bh
    signalGen.color = QColor(0xD3, 0x2F, 0x2F);  // 红色，突出为数据源头
    signalGen.enabled = false;  // 默认不启用，需用户手动激活
    m_blocks["signal_generator"] = signalGen;

    qreal x = startX + srcW + gapX;  // 移动到第 2 列

    // ---- 第 2 列：数据源切换开关 + Real/离线分析 ----
    qreal srcY1 = 50;  // Real 的 y 坐标
    qreal srcY2 = srcY1 + srcH + switchH + 10;  // 离线分析的 y 坐标

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

    // ---- 第 3 列：Filter 过滤块（flow.md §8.1 Filter 角色 UI 前置；
    //      多个 CAN 通道块收编为单块，数据流过滤统一在此配置） ----
    const qreal filterW = bw - 30;

    BlockItem filt;
    filt.id = "filter";
    filt.title = QStringLiteral("Filter 过滤");
    filt.icon = "";
    filt.category = "filter";
    filt.moduleName = "filter";
    filt.rect = QRectF(x, 50, filterW, bh);  // 与 Real 同一水平线
    filt.color = QColor(0x00, 0x79, 0x8C);
    m_blocks["filter"] = filt;


        x += filterW + gapX;
        
        // ---- 第 4 列：CAN parser（与 Filter 块同一水平线；原「DBC 数据库」，
        //      截图反馈 2026-08-23 改名，与 CAN Flow 语境一致） ----
    BlockItem dbc;
    dbc.id = "database";
    dbc.title = QStringLiteral("CAN parser");
    dbc.icon = "";
    dbc.category = "database";
    dbc.rect = QRectF(x, 50, bw, bh);  // 与 Filter 同一水平线
    dbc.color = QColor(0x7B, 0x1F, 0xA2);
    m_blocks["database"] = dbc;

    x += filterW + gapX;
    
    // ---- 第 4 列：CAN parser（与 Filter 块同一水平线；原「DBC 数据库」，

    x += bw + gapX;

    // ---- 第 5 列：分析模块 (垂直堆叠：Trace / Graphic / Record / Watcher)
    struct ModDef { QString id; QString icon; QString title; QColor color; QString moduleName; };
    ModDef mods[] = {
        {"trace1",   "", "帧列表 1",           QColor(0x21, 0x96, 0xF3), "trace"},
        {"graphic1", "", "时序波形 1",         QColor(0xF4, 0x43, 0x36), "graphic"},
        {"record",   "", "Record",           QColor(0xFF, 0x98, 0x00), ""},
        {"watcher",  "", "Watcher",          QColor(0x4C, 0xAF, 0x50), ""},
    };
    int modCount = 4;
    int modW = 140;
    int modGap = 16;
    qreal modY = 30;

    for (int i = 0; i < modCount; ++i) {
        BlockItem b;
        b.id = mods[i].id;
        b.title = mods[i].title;
        b.icon = mods[i].icon;
        b.category = "module";
        b.moduleName = mods[i].moduleName;
        b.rect = QRectF(x, modY + i * (bh + modGap), modW, bh);
        b.color = mods[i].color;
        // 默认不启用：除 Trace/Graphic 外的观测类模块 (Watcher/Record) 默认禁用
        if (mods[i].id == QLatin1String("trace1") || mods[i].id == QLatin1String("graphic1"))
            b.enabled = true;
        else
            b.enabled = false;
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
    // 数据流架构：信号发生器 → Real → Filter → CAN parser；文件回放可切换注入 Real
    addConn("signal_generator", "source_real");  // 信号发生器数据注入到 Real
    addConn("source_real", "filter");            // Real → Filter（主路径）
    addConn("source_file", "filter");            // 文件回放直接连 Filter
    addConn("filter", "database");
    // DBC → 各模块
    addConn("database", "trace1");
    addConn("database", "graphic1");
    addConn("database", "watcher");
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
        if (b.category == "module" || b.category == "filter") {
            // Trace / Graphic 块独占一行，无子实例；其他模块与 Filter 块
            // （实例行 = 过滤规则行）根据行数动态调整高度
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
        
        // 数据源块：统一使用绿色激活、灰色非激活；信号发生器独立控制
        if (b.category == "source") {
            // 统一激活颜色（绿色）、非激活颜色（灰色）
            b.color = active ? QColor(0x4C, 0xAF, 0x50) : QColor(0x80, 0x80, 0x80);
            
            if (b.id == "source_real") {
                b.title = QStringLiteral("Real 实时");
                // Real 的状态由 activeSourceId 控制
                active = (b.id == activeSourceId());
            } else if (b.id == "source_file") {
                b.title = QStringLiteral("离线分析");
                // 离线分析的状态由 activeSourceId 控制
                active = (b.id == activeSourceId());
            } else if (b.id == "signal_generator") {
                b.title = QStringLiteral("信号发生器");
                // signal_generator 直接显示 enabled 状态
                active = b.enabled;
            }
        }

        auto *item = new SetupBlockGfx(b.rect, b.icon, b.title, b.color, active,
                                        b.category == "source");
        // 传递实例列表给渲染图元（Filter 块实例行 = 过滤规则行）
        if ((b.category == "module" || b.category == "filter")
            && b.moduleName != "trace" && b.moduleName != "graphic") {
            QStringList instTitles;
            for (const auto &inst : b.instances)
                instTitles << inst.title;
            item->setInstances(instTitles);
        }
        if (b.category == "filter")
            item->setRuleMode(true);
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

    // 重建 gfx 后重新投影灯态（块使能/数据流/异常状态存在视图层）
    updateBlockLamps();
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
        
        // source 类型块的激活状态计算规则：
        // - signal_generator: 直接使用 enabled 状态
        // - source_real/source_file: 由 activeSourceId 控制
        if (fromIt->category == "source") {
            if (fromIt->id == "signal_generator") {
                fromActive = fromIt->enabled;  // ✅ 信号发生器独立控制
            } else {
                fromActive = (fromIt->id == activeSourceId());  // Real/File 受控于 m_source
            }
        }

        // 非 active 数据源的连线不绘制（避免 4 条线交叉）
        // ❌ 移除这个限制，因为 signal_generator 是独立的
        // if (fromIt->category == "source" && !fromActive)
        //     continue;

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
            // 数据源块以「当前激活数据源」点亮（非激活侧呈灰态）
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
    // 使能变化即时刷新灯态（无需等闪烁 tick）
    updateBlockLamps();
}

void MeasurementSetupView::updateBlockLamps()
{
    // 数据流活跃判定：测量运行中且最近 1.5s 内有帧到达
    // （无新帧超过 1.5s → 视为流静止，绿闪退回常亮待命）
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool flowActive = m_running && m_lastFrameMs > 0
                            && (now - m_lastFrameMs) < 1500;
    // 单块灯态：异常优先（含数据源块）→ 未使能不亮 → 待命/流闪
    auto lampFor = [this, flowActive](const QString &id, const BlockItem &b) {
        if (m_blockErrors.contains(id))
            return BlockLamp::Error;
        const bool isSource = b.category == QStringLiteral("source");
        const bool active = isSource ? (b.id == activeSourceId()) : b.enabled;
        if (!active)
            return BlockLamp::None;  // 未使能：不亮灯（块体灰化已表达）
        // 数据源块正常态不亮灯（仅异常红闪）
        if (isSource)
            return BlockLamp::None;
        return flowActive ? BlockLamp::Flow : BlockLamp::Idle;
    };
    for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        auto *gfx = dynamic_cast<SetupBlockGfx *>(it.value().gfxItem);
        if (gfx)
            gfx->setLamp(lampFor(it.key(), it.value()), m_blinkOn);
    }
    // 无数据流且无异常：闪烁驱动停转（省电；灯态已静态化）
    if (!flowActive && m_blockErrors.isEmpty() && m_blinkTimer)
        m_blinkTimer->stop();
}

void MeasurementSetupView::setBlockError(const QString &blockId, bool on)
{
    if (on && !m_blocks.contains(blockId))
        return;  // 未知块（拓扑未建/ID 拼写）——静默忽略
    const bool changed = on ? !m_blockErrors.contains(blockId)
                            : m_blockErrors.remove(blockId) > 0;
    if (!changed)
        return;
    if (on && m_blinkTimer && !m_blinkTimer->isActive()) {
        m_blinkOn = true;  // 错误出现立即以亮相位起闪
        m_blinkTimer->start();
    }
    updateBlockLamps();
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
    setBlockEnabled(id, !it->enabled);
}

void MeasurementSetupView::setBlockEnabled(const QString &id, bool enabled)
{
    auto it = m_blocks.find(id);
    if (it == m_blocks.end()) return;
    auto &b = it.value();
    if (b.category == "source") {
        // signal_generator 允许通过双击启用/禁用；real/file 不可直接更改
        if (b.id != "signal_generator")
            return;
    }
    if (b.enabled == enabled) return;

    b.enabled = enabled;
    rebuildScene();
    emit moduleToggled(b.id, b.moduleName, b.enabled);
}

void MeasurementSetupView::openBlockConfig(const QString &blockId)
{
    // 块配置统一入口（已启用块单击 / 双击 / 右键「配置」共用）
    auto it = m_blocks.find(blockId);
    if (it == m_blocks.end()) return;
    const auto &b = it.value();

    if (b.category == "filter") {
        // Filter 块：配置数据流过滤规则
        showFilterConfigDialog();
    } else if (b.category == "database") {
        // 数据库块：选择 DBC 文件
        showDbcSelectDialog();
    } else if (b.category == "module") {
        if (b.moduleName == "trace") {
            emit moduleOpened("trace", b.id);
        } else if (b.moduleName == "graphic") {
            emit moduleOpened("graphic", b.id);
        } else {
            emit moduleOpened(b.id, "");
        }
    }
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
        return true;  // missing → enabled by default
    return it->enabled;
}

QHash<QString, bool> MeasurementSetupView::blockEnabledMap() const
{
    QHash<QString, bool> out;
    for (auto it = m_blocks.constBegin(); it != m_blocks.constEnd(); ++it)
        out.insert(it.key(), it->enabled);
    return out;
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
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    // 检查是否点击了切换开关
    if (m_switchRect.contains(scenePos)) {
        Source newSrc = (m_source == Source::Hardware) ? Source::File : Source::Hardware;
        setSource(newSrc);
        emit sourceChanged(static_cast<int>(newSrc));
        m_lastClickTime = now;  // 记录点击时间
        return;
    }

    QString moduleId, instanceId;
    if (instanceAt(scenePos, moduleId, instanceId)) {
        emit moduleOpened(moduleId, instanceId);
        m_lastClickTime = now;  // 记录点击时间
        return;
    }

    auto *b = blockAt(scenePos);
    if (!b) return;

    // ✅ 统一交互规则：
    // - data source: Real、File 互斥，单击自动切换；signal_generator 独立开关
    // - Filter/CAN parser/Watcher/Record：单击不操作，只允许双击打开配置
    // - Trace/Graphic 独立块：单击切换使能状态
    if (b->category == "source") {
        if (b->id == "signal_generator") {
            // 信号发生器：独立的开关，可以手动启用/禁用
            setBlockEnabled(b->id, !b->enabled);
            m_lastClickTime = now;  // 记录点击时间
        } else {
            // Real / File 互斥切换：单击自动激活当前块，停用另一个
            Source newSrc = (b->id == "source_real") ? Source::Hardware : Source::File;
            setSource(newSrc);
            emit sourceChanged(static_cast<int>(newSrc));
            m_lastClickTime = now;  // 记录点击时间
        }
    } else if (b->category == "filter" || b->category == "database" ||
               b->id == "watcher" || b->id == "record") {
        // ✅ Filter/CAN parser/Watcher/Record：单击切换使能状态，同时记录时间供双击判断
        setBlockEnabled(b->id, !b->enabled);
        m_lastClickTime = now;  // 记录点击时间
    } else if (b->category == "module" && (b->moduleName == "trace" || b->moduleName == "graphic")) {
        // Trace/Graphic 独立块：单击切换使能状态
        setBlockEnabled(b->id, !b->enabled);
        m_lastClickTime = now;  // 记录点击时间
    }
}

void MeasurementSetupView::onSceneDoubleClicked(const QPointF &scenePos)
{
    auto *b = blockAt(scenePos);
    if (!b) return;

    // ✅ 双击判断：在短时间内连续两次点击 → 打开配置页/Tab
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool isFastDoubleClick = (now - m_lastClickTime < DOUBLE_CLICK_INTERVAL);
    
    // 数据源块和模块块的交互规则分开处理
    if (b->category == "source") {
        if (b->id == "source_real") {
            // Real 实时：双击打开连接配置（仅当已启用时）
            if (isFastDoubleClick && b->enabled) {
                emit realBlockClicked();
            }
        } else if (b->id == "source_file") {
            // 离线分析：双击打开文件选择（仅当已启用时）
            if (isFastDoubleClick && b->enabled) {
                emit fileBlockClicked();
            }
        } else if (b->id == "signal_generator") {
            // 信号发生器：双击打开发送配置
            // ✅ 无论使能状态都允许打开配置
            if (isFastDoubleClick) {
                emit sendPageOpened();
            }
        }
    } else if (b->category == "filter" || b->category == "database" || 
               b->id == "watcher" || b->id == "record") {
        // ✅ 配置类块：双击行为等效于右键菜单→选择配置项
        //   - Filter 过滤块 (category="filter") → 右键→"配置过滤条件..." → showFilterConfigDialog()
        //   - CAN parser 块 (category="database") → 右键→"选择 DBC 文件..." → showDbcSelectDialog()
        //   - Watcher 观测块 (id="watcher") → 右键→"观测变量与统计设置..." → moduleOpened("watcher", "")
        //   - Record 录制块 (id="record") → 右键→"配置录制参数..." → moduleOpened("record", "")
        // ✅ 关键修复：Qt 的 sceneDoubleClicked 信号已经保证是双击事件，不需要 isFastDoubleClick 判断
        {
            emit moduleOpened(b->id, "");
        }
    } else if (b->category == "module" && (b->moduleName == "trace" || b->moduleName == "graphic")) {
        // ✅ Trace / Graphic 独立块 (如 trace1, graphic1)
        // 双击直接跳转到该块对应的标签页（移除添加同类块的错误关联）
        if (isFastDoubleClick) {
            emit moduleOpened(b->moduleName, b->id);
        }
        return;
    }
}

void MeasurementSetupView::setSource(Source src)
{
    m_source = src;
    // 更新数据源块的 enabled 状态（只控制 file 块，不干扰 signal_generator 和 source_real）
    if (m_blocks.contains("source_file"))
        m_blocks["source_file"].enabled = (src == Source::File);
    
    // 当切换到 Hardware 模式时，确保 source_real 处于 enabled 状态
    if (src == Source::Hardware && m_blocks.contains("source_real")) {
        m_blocks["source_real"].enabled = true;  // 硬件模式自动启用 Real
    }
    
    // 更新连线：将旧数据源的连线替换为新数据源
    // （去重——同一数据源来回切换不再累积重叠连线）
    QString oldId = (src == Source::Hardware) ? "source_file" : "source_real";
    QString newId = (src == Source::Hardware) ? "source_real" : "source_file";
    for (auto &conn : m_connections) {
        if (conn.fromId == oldId)
            conn.fromId = newId;
    }
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        for (int j = 0; j < i; ++j) {
            if (m_connections[j].fromId == m_connections[i].fromId
                && m_connections[j].toId == m_connections[i].toId) {
                m_connections.removeAt(i);
                break;
            }
        }
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
    // 数据流指示：记录最近帧到达时刻（功能块绿灯闪烁的驱动源；
    // 帧数统计由 MainWindow 状态栏统一显示）
    m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
    if (!m_blinkTimer->isActive()) {
        m_blinkOn = true;  // 首帧起闪从亮相位开始
        m_blinkTimer->start();
        updateBlockLamps();
    }
}

void MeasurementSetupView::setRunning(bool running)
{
    // 纯状态复位（离线回放结束/未真正启动时由壳经 flow 模块调用）：
    // 只同步按钮态，不发 measurementToggled——启停编排归壳侧，避免二次停止
    m_running = running;
    m_startAct->setEnabled(!running);
    m_stopAct->setEnabled(running);
    // 测量停止：数据流状态复位（灯回待命常亮/熄灭；异常标记保留——
    // 未消除的红闪持续提示）
    m_lastFrameMs = 0;
    updateBlockLamps();
}

void MeasurementSetupView::onStartClicked()
{
    setRunning(true);
    emit measurementToggled(true);
}

void MeasurementSetupView::onStopClicked()
{
    setRunning(false);
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

        auto *actJump = m_rightMenu->addAction("跳转到此标签页");
        connect(actJump, &QAction::triggered, this, [this, instModuleId, instInstanceId]() {
            emit moduleOpened(instModuleId, instInstanceId);
        });

        auto *actClose = m_rightMenu->addAction("删除此实例");
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

    // ---- Filter 过滤块 ----
    else if (blockCategory == "filter") {
        // 数据流过滤配置统一入口（原各 CAN 通道块右键收编合并至此）
        auto *actFilter = m_rightMenu->addAction("配置过滤条件...");
        actFilter->setStatusTip("设置 CAN ID 范围、帧类型、方向等过滤规则");
        connect(actFilter, &QAction::triggered, this, [this]() {
            showFilterConfigDialog();
        });

        // 清空规则（有规则时显示）
        const int ruleCount = block->instances.size();
        if (ruleCount > 0) {
            auto *actClear = m_rightMenu->addAction(
                QString("清空过滤规则（%1 条）").arg(ruleCount));
            actClear->setStatusTip("移除全部规则，数据流直连不过滤");
            connect(actClear, &QAction::triggered, this, [this]() {
                clearFilterRules();
            });
        }

        m_rightMenu->addSeparator();

        auto *actToggle = m_rightMenu->addAction(blockEnabled ? "禁用过滤" : "启用过滤");
        connect(actToggle, &QAction::triggered, this, [this, blockId]() {
            toggleBlock(blockId);
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
            auto *actAdd = m_rightMenu->addAction(
                svgIcon(":/icons/plus.svg",
                        ThemeManager::instance()->currentTheme().text, 16),
                "添加 Trace 视图");
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
        } else if (blockId == "watcher") {
            auto *actCfg = m_rightMenu->addAction("观测变量与统计设置...");
            actCfg->setStatusTip("打开 Watcher 观测页（变量 Watch 列表 + 总线统计）");
            connect(actCfg, &QAction::triggered, this, [this]() {
                emit moduleOpened("watcher", "");
            });
        } else if (blockId == "record") {
            auto *actCfg = m_rightMenu->addAction(" 配置录制参数...");
            actCfg->setStatusTip("设置录制文件路径和格式");
            connect(actCfg, &QAction::triggered, this, [this]() {
                emit moduleOpened("record", "");
            });
        }

        m_rightMenu->addSeparator();

        // 模块块统一配置入口：打开/切换对应标签页
        auto *actOpen = m_rightMenu->addAction("配置 / 跳转标签页");
        actOpen->setStatusTip("在中心区域打开/切换到该模块的标签页");
        connect(actOpen, &QAction::triggered, this, [this, blockModule, blockId]() {
            if (blockModule == "trace" || blockModule == "graphic")
                emit moduleOpened(blockModule, blockId);
            else
                emit moduleOpened(blockId, "");
        });

        m_rightMenu->addSeparator();

        auto *actToggle = m_rightMenu->addAction(blockEnabled ? "禁用模块" : "启用模块");
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
        {"watcher",  "", "Watcher",          QColor(0x4C, 0xAF, 0x50)},
        {"record",   "", "Record",           QColor(0xFF, 0x98, 0x00)},
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
//  Filter 块过滤规则配置对话框（数据流过滤统一配置入口）
// ============================================================
// NOTE: 原「CAN 通道 N」各自的过滤对话框已随通道块收编合并为一个
//       Filter 块；规则以摘要行形式挂在 Filter 块实例列表内展示
void MeasurementSetupView::showFilterConfigDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Filter 过滤规则配置");
    dlg.setMinimumWidth(440);
    auto *lay = new QVBoxLayout(&dlg);

    auto *hint = new QLabel("规则列表（命中任一规则即放行，空列表 = 不过滤）:", &dlg);
    lay->addWidget(hint);

    // 规则列表（实时反映 Filter 块实例行）
    auto *ruleList = new QListWidget(&dlg);
    ruleList->setAlternatingRowColors(true);
    ruleList->setMinimumHeight(140);
    ruleList->setMaximumHeight(200);
    lay->addWidget(ruleList);

    // 刷新规则列表显示
    auto refreshList = [this, ruleList]() {
        ruleList->clear();
        for (const auto &r : filterRules())
            ruleList->addItem(r);
    };
    refreshList();

    // ---- 新建规则表单 ----
    auto *formGroup = new QGroupBox("新建规则", &dlg);
    auto *form = new QFormLayout(formGroup);

    // CAN ID 范围
    auto *idMin = new QSpinBox(formGroup);
    idMin->setRange(0, 0x7FF);
    idMin->setDisplayIntegerBase(16);
    idMin->setPrefix("0x");
    idMin->setValue(0);

    auto *idMax = new QSpinBox(formGroup);
    idMax->setRange(0, 0x7FF);
    idMax->setDisplayIntegerBase(16);
    idMax->setPrefix("0x");
    idMax->setValue(0x7FF);

    auto *idRangeWidget = new QWidget(formGroup);
    auto *idRangeLay = new QHBoxLayout(idRangeWidget);
    idRangeLay->setContentsMargins(0, 0, 0, 0);
    idRangeLay->addWidget(idMin);
    idRangeLay->addWidget(new QLabel("~", idRangeWidget));
    idRangeLay->addWidget(idMax);
    form->addRow("ID 范围:", idRangeWidget);

    // 帧类型选项
    auto *chkStd   = new QCheckBox("标准帧", formGroup);
    auto *chkExt   = new QCheckBox("扩展帧", formGroup);
    auto *chkFD    = new QCheckBox("CAN FD", formGroup);
    auto *chkRTR   = new QCheckBox("RTR", formGroup);
    chkStd->setChecked(true);
    chkExt->setChecked(true);

    auto *typeWidget = new QWidget(formGroup);
    auto *typeLay = new QHBoxLayout(typeWidget);
    typeLay->setContentsMargins(0, 0, 0, 0);
    typeLay->addWidget(chkStd);
    typeLay->addWidget(chkExt);
    typeLay->addWidget(chkFD);
    typeLay->addWidget(chkRTR);
    form->addRow("帧类型:", typeWidget);

    // 方向过滤
    auto *comboDir = new QComboBox(formGroup);
    comboDir->addItems({"全部", "仅 Tx (发送)", "仅 Rx (接收)"});
    form->addRow("方向:", comboDir);

    lay->addWidget(formGroup);

    // ---- 增删按钮 ----
    auto *btnLay = new QHBoxLayout();
    auto *addBtn = new QPushButton("+ 添加规则", &dlg);
    auto *delBtn = new QPushButton("- 移除选中规则", &dlg);
    btnLay->addWidget(addBtn);
    btnLay->addWidget(delBtn);
    btnLay->addStretch();
    lay->addLayout(btnLay);

    // 添加规则：立即生效（挂到 Filter 块规则行并刷新画布）
    QObject::connect(addBtn, &QPushButton::clicked, this,
                     [this, idMin, idMax, chkStd, chkExt, chkFD, chkRTR, comboDir, &refreshList]() {
        QStringList types;
        if (chkStd->isChecked()) types << "Std";
        if (chkExt->isChecked()) types << "Ext";
        if (chkFD->isChecked())  types << "FD";
        if (chkRTR->isChecked()) types << "RTR";
        if (types.isEmpty())
            types << "全部";

        auto it = m_blocks.find("filter");
        if (it == m_blocks.end())
            return;
        InstanceItem rule;
        rule.id = QStringLiteral("rule%1").arg(it->instances.size() + 1);
        rule.title = QStringLiteral("ID 0x%1~0x%2 · %3 · %4")
                         .arg(idMin->value(), 0, 16)
                         .arg(idMax->value(), 0, 16)
                         .arg(types.join("+"))
                         .arg(comboDir->currentText());
        it->instances.append(rule);

        rebuildScene();
        refreshList();
        emit filterRulesChanged(filterRules());
        qDebug() << "Filter rule added:" << rule.title;
    });

    // 移除选中规则：立即生效
    QObject::connect(delBtn, &QPushButton::clicked, this,
                     [this, ruleList, &refreshList]() {
        int row = ruleList->currentRow();
        auto it = m_blocks.find("filter");
        if (row < 0 || it == m_blocks.end() || row >= it->instances.size())
            return;
        const QString removed = it->instances[row].title;
        it->instances.removeAt(row);

        rebuildScene();
        refreshList();
        emit filterRulesChanged(filterRules());
        qDebug() << "Filter rule removed:" << removed;
    });

    // 关闭（规则增删即时生效，无需确认按钮）
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    lay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

QStringList MeasurementSetupView::filterRules() const
{
    QStringList rules;
    auto it = m_blocks.constFind("filter");
    if (it == m_blocks.constEnd())
        return rules;
    for (const auto &inst : it->instances)
        rules << inst.title;
    return rules;
}

void MeasurementSetupView::clearFilterRules()
{
    auto it = m_blocks.find("filter");
    if (it == m_blocks.end() || it->instances.isEmpty())
        return;
    it->instances.clear();
    rebuildScene();
    emit filterRulesChanged(QStringList());
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

