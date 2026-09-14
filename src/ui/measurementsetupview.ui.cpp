#include "measurementsetupview.h"
#include "ui/measurementsetupview.gfx.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <QVBoxLayout>
#include <QApplication>
#include <QPainter>
#include <QScrollBar>

// ============================================================
//  Constructor & setupUi — 构造函数与 UI 布局
// ============================================================

MeasurementSetupView::MeasurementSetupView(QWidget *parent)
    : QWidget(parent)
{
    // Lamp blink timer driver (start before scene building; rebuildScene will project lamp states at end;
    // runs only when data flow active or errors exist, stops otherwise to avoid idle refreshes)
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkOn = !m_blinkOn;
        updateBlockLamps();
    });
    
    setupUi();
    buildTopology();  // buildTopology() internally calls relayoutModuleBlocks() → rebuildScene()
    
    m_recentFiles.clear();
}

void MeasurementSetupView::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // ---- Toolbar ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(20, 20));
    
    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_startAct = m_toolbar->addAction(svgIcon(":/icons/play.svg", iconCol, 20), "开始");
    
    m_stopAct = m_toolbar->addAction(svgIcon(":/icons/stop.svg", iconCol, 20), "停止");
    m_stopAct->setEnabled(false);
    
    m_toolbar->addSeparator();
    
    layout->addWidget(m_toolbar);
    
    // ---- Canvas ----
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
    
    // ---- Signal connections ----
    connect(m_startAct, &QAction::triggered, this, &MeasurementSetupView::onStartClicked);
    connect(m_stopAct, &QAction::triggered, this, &MeasurementSetupView::onStopClicked);
    
    connect(scene, &SetupScene::sceneClicked, this, &MeasurementSetupView::onSceneClicked);
    connect(scene, &SetupScene::sceneDoubleClicked, this, &MeasurementSetupView::onSceneDoubleClicked);
    connect(scene, &SetupScene::sceneRightClicked, this, &MeasurementSetupView::onSceneRightClicked);
}

// ============================================================
//  buildTopology — 拓扑构建（块 + 连线定义）
// ============================================================

void MeasurementSetupView::buildTopology()
{
    m_blocks.clear();
    
    // ---- Layout parameters (arranged left-to-right) ----
    const qreal bw = 220;   // Block width
    const qreal bh = 60;    // Block height (unified height)
    const qreal gapX = 60;  // Horizontal spacing (column spacing)
    const qreal startX = 40;
    
    // ---- Data source block related parameters (unified height bh, no extra height) ----
    const qreal srcW = 160;
    const qreal srcH = bh;  // Unified height with main flow blocks
    const qreal switchW = 70;
    const qreal switchH = 32;
    
    // ---- Column 1: CAN signal generator (only data source root) ----
    BlockItem signalGen;
    signalGen.id = "signal_generator";
    signalGen.title = "信号发生器";  // Remove "CAN" prefix
    signalGen.icon = "";
    signalGen.category = "source";
    signalGen.moduleName = "signal_generator";
    signalGen.rect = QRectF(startX, 50, srcW, srcH);  // Use unified height srcH=bh
    signalGen.color = QColor(0xD3, 0x2F, 0x2F);  // Red, highlighting as data source root
    signalGen.enabled = false;  // Default disabled, user must manually activate
    m_blocks["signal_generator"] = signalGen;
    
    qreal x = startX + srcW + gapX;  // Move to column 2
    
    // ---- Column 2: Data source switch + Real/offline analysis ----
    qreal srcY1 = 50;  // Real's y-coordinate
    qreal srcY2 = srcY1 + srcH + switchH + 10;  // Offline analysis's y-coordinate
    
    // Real (hardware real-time)
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
    
    // Offline analysis (file data source)
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
    
    // Switch position (between the two data source blocks)
    m_switchRect = QRectF(x + (srcW - switchW) / 2, srcY1 + srcH + 5, switchW, switchH);
    
    x += srcW + gapX;
    
    // ---- Column 3: Filter block (flow.md §8.1 Filter role UI advanced;
    //      multiple CAN channel blocks consolidated into single block;
    //      data stream filtering unified here) ----
    const qreal filterW = bw - 30;
    
    BlockItem filt;
    filt.id = "filter";
    filt.title = QStringLiteral("Filter");
    filt.icon = "";
    filt.category = "filter";
    filt.moduleName = "filter";
    filt.rect = QRectF(x, 50, filterW, bh);  // Same horizontal line as Real
    filt.color = QColor(0x00, 0x79, 0x8C);
    m_blocks["filter"] = filt;
    
    x += filterW + gapX;
    
    // ---- Column 4: CAN parser (same horizontal line as Filter block; originally "DBC Database",
    //      renamed based on screenshot feedback 2026-08-23 to align with CAN Flow terminology) ----
    BlockItem dbc;
    dbc.id = "database";
    dbc.title = QStringLiteral("CAN parser");
    dbc.icon = "";
    dbc.category = "database";
    dbc.rect = QRectF(x, 50, bw, bh);  // Same horizontal line as Filter
    dbc.color = QColor(0x7B, 0x1F, 0xA2);
    m_blocks["database"] = dbc;
    
    x += filterW + gapX;
    
    x += bw + gapX;
    
    // ---- Column 5: Analysis modules (vertical stacking: Trace/Graphic/Record/Watcher)
    struct ModDef { QString id; QString icon; QString title; QColor color; QString moduleName; };
    ModDef mods[] = {
        {"trace1",   "", "帧列表 1",           QColor(0x21, 0x96, 0xF3), "trace"},
        {"graphic1", "", "时序波形 1",         QColor(0xF4, 0x43, 0x36), "graphic"},
        {"record",   "", "录制 Record",      QColor(0xFF, 0x98, 0x00), ""},
        {"watcher",  "", "Watcher 观测",     QColor(0x4C, 0xAF, 0x50), ""},
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
        // Default disabled: observation-type modules (Watcher/Record) except Trace/Graphic default to disabled
        if (mods[i].id == QLatin1String("trace1") || mods[i].id == QLatin1String("graphic1"))
            b.enabled = true;
        else
            b.enabled = false;
        m_blocks[mods[i].id] = b;
    }
    
    // ---- Connection definition ----
    m_connections.clear();
    auto addConn = [this](const QString &from, const QString &to) {
        Connection c;
        c.fromId = from;
        c.toId = to;
        c.pathItem = nullptr;
        m_connections.append(c);
    };
    // Data flow architecture: signal generator → Real → Filter → CAN parser; file playback can switch injection into Real
    addConn("signal_generator", "source_real");  // Signal generator data injected into Real
    addConn("source_real", "filter");            // Real → Filter (main path)
    addConn("source_file", "filter");            // File playback directly connects to Filter
    addConn("filter", "database");
    // DBC → all modules
    addConn("database", "trace1");
    addConn("database", "graphic1");
    addConn("database", "watcher");
    addConn("database", "record");
    
    // Module blocks vertical stacking
    relayoutModuleBlocks();
}
