#include "graphicview.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "signalconfigdialog.h"
#include "dbcsignalpickerdialog.h"
#include "core/dbcmanager.h"
#include "core/appconfig.h"
#include "core/samplestore.h"
#include "utils/canutils.h"
#include "utils/svg_icon.h"
#include <QMessageBox>
#include <spdlog/spdlog.h>

#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QAbstractItemModel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QMenu>
#include <QAction>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QThread>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QComboBox>
#include <QColorDialog>
#include <QPixmap>
#include <QRubberBand>
#include <QShortcut>
#include <QKeySequence>
#include <QDialog>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <cmath>
#include <algorithm>
#include <functional>

#include "qcustomplot.h"
#include "thememanager.h"
#include "utils/svg_icon.h"
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"

// ============================================================
//  辅助：工具栏图标（VS Code 线条风格 SVG）
//  常态 = 主题前景色，选中态 = 白色（checked 时 QSS 为 accent 背景）；
//  按钮记 iconName 属性，主题切换后由 updateToolbarIcons() 统一重刷
// ============================================================
static void applyToolBarIcon(QToolButton *btn, const QString &iconName)
{
    const QString path = QStringLiteral(":/icons/%1.svg").arg(iconName);
    const QColor normal = ThemeManager::instance()->currentTheme().text;
    QIcon icon;
    icon.addPixmap(renderSvgPixmap(path, normal.name(), 16), QIcon::Normal, QIcon::Off);
    icon.addPixmap(renderSvgPixmap(path, QStringLiteral("#ffffff"), 16), QIcon::Normal, QIcon::On);
    btn->setProperty("iconName", iconName);
    btn->setIcon(icon);
}

// ============================================================
//  辅助：QCustomPlot 子类 — 支持卡尺拖动 + 鼠标滚轮缩放
// ============================================================

class CursorPlot : public QCustomPlot
{
public:
    explicit CursorPlot(QWidget *parent = nullptr) : QCustomPlot(parent) {}

    std::function<void(QMouseEvent*)> onMousePress;
    std::function<void(QMouseEvent*)> onMouseMove;
    std::function<void(QMouseEvent*)> onMouseRelease;
    std::function<void(QMouseEvent*)> onMouseDoubleClick;
    std::function<void(QWheelEvent*)> onWheel;
    std::function<void(QContextMenuEvent*)> onContextMenu;
    std::function<void(QResizeEvent*)> onResize;
    std::function<void()> onLeave;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (onMousePress) onMousePress(event);
        if (!event->isAccepted())
            QCustomPlot::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (onMouseMove) onMouseMove(event);
        if (!event->isAccepted())
            QCustomPlot::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (onMouseRelease) onMouseRelease(event);
        if (!event->isAccepted())
            QCustomPlot::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (onMouseDoubleClick) onMouseDoubleClick(event);
        if (!event->isAccepted())
            QCustomPlot::mouseDoubleClickEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        if (onLeave) onLeave();
        QCustomPlot::leaveEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override
    {
        if (onWheel) onWheel(event);
        if (!event->isAccepted())
            QCustomPlot::wheelEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (onContextMenu) onContextMenu(event);
        else
            QCustomPlot::contextMenuEvent(event);
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QCustomPlot::resizeEvent(event);
        if (onResize) onResize(event);
    }
};

// ============================================================
//  时间轴 Ticker — mm:ss.ms 格式 (CANoe 风格)
// ============================================================
class TimeTicker : public QCPAxisTicker
{
public:
    QString getTickLabel(double tick, const QLocale &locale, QChar formatChar, int precision) override
    {
        Q_UNUSED(locale); Q_UNUSED(formatChar); Q_UNUSED(precision);
        if (tick < 0) return {};
        // Match Trace Absolute Time column (file/measurement seconds, not PC wall clock)
        return CanUtils::formatTime(tick, 6);
    }
};

// ============================================================
//  GraphicPalette — CANoe 经典浅色基准 / 深色同构映射
// ============================================================

GraphicPalette GraphicPalette::canoeLight()
{
    GraphicPalette p;
    p.canvas      = QColor(0xFF, 0xFF, 0xFF);   // 白底（CANoe 经典）
    p.grid        = QColor(0xD9, 0xD9, 0xD9);   // 浅灰实线网格
    p.trackSep    = QColor(0xBF, 0xBF, 0xBF);   // 轨道分隔线
    p.axis        = QColor(0x00, 0x00, 0x00);   // 黑色轴线/刻度（CANoe）
    p.axisText    = QColor(0x00, 0x00, 0x00);
    p.timeLine    = QColor(0xFF, 0xD9, 0x00);   // 当前时间线（黄，白底可辨识）
    p.cursor1     = QColor(0x00, 0x00, 0x00);   // 测量卡尺 1：黑实线
    p.cursor2     = QColor(0x1E, 0x64, 0xC8);   // 测量卡尺 2：深蓝实线
    p.trackCursor = QColor(0x80, 0x80, 0x80);   // 鼠标跟踪线：灰点线
    p.nameTagBg     = QColor(0xFF, 0xFF, 0xFF, 235);
    p.nameTagFg     = QColor(0x00, 0x00, 0x00);
    p.nameTagBorder = QColor(0xBF, 0xBF, 0xBF);
    p.dimCurve    = QColor(0xB0, 0xB0, 0xB0);   // 聚焦模式未选中曲线置灰
    return p;
}

GraphicPalette GraphicPalette::canoeDark()
{
    // Dark palette retained for API compatibility; Light-only UI always uses canoeLight()
    return canoeLight();
}

// ============================================================
//  GraphicView 实现
// ============================================================

GraphicView::GraphicView(QWidget *parent)
    : QWidget(parent)
{
    m_palette = GraphicPalette::canoeLight();

    setupUi();
    setAcceptDrops(true);

    const auto *cfg = AppConfig::instance();
    const int fps = qBound(10, cfg->getInt(QStringLiteral("graphic.fps"), 30), 60);
    m_replotIntervalMs = qMax(16, 1000 / fps);
    m_rawMaxCapacity = qBound(8192, cfg->getInt(QStringLiteral("graphic.maxSamples"), 200000), 1000000);

    m_replotTimer.setInterval(m_replotIntervalMs);
    m_replotTimer.setSingleShot(true);
    connect(&m_replotTimer, &QTimer::timeout, this, [this]() { onReplotTimeout(); });

    // Phase B3: pull shared SampleStore on a timer (decode happens once in the store)
    m_samplePullTimer.setInterval(m_replotIntervalMs);
    connect(&m_samplePullTimer, &QTimer::timeout, this, &GraphicView::onSamplePullTimer);
    m_samplePullTimer.start();

    // 信号列表值刷新：200ms
    m_valueTimer.setInterval(VALUE_UPDATE_MS);
    connect(&m_valueTimer, &QTimer::timeout, this, [this]() { updateSignalValues(); });
    m_valueTimer.start();

    // 缩放历史：滚轮防抖合并（操作前状态已在滚动开始时 push，超时无需再记）
    m_zoomPushTimer.setInterval(500);
    m_zoomPushTimer.setSingleShot(true);

    // 主题切换 → 全视图重刷配色
    // DEF-08 字符串信号：ThemeManager 定义于 data.dll，跨 DLL PMF connect 断连
    auto *paletteRelay = new SignalRelay(this);
    paletteRelay->fire0 = [this]() { applyPalette(); };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            paletteRelay, SLOT(fire()));

    // 卡尺/时间线/跟踪线用 ptAbsolute 像素定位（updateCursorDecorations 统一维护），
    // 不依赖任何业务轴生命周期
    ensureCurrentTimeLine();
    ensureTrackLine();
}

GraphicView::~GraphicView()
{
    for (auto &sd : m_signals)
        unsubscribeStore(sd);
}

QColor GraphicView::autoColor(int index)
{
    // CANoe/Vector 经典高饱和 16 色板（深浅底通用，白底对比度优于 Material 色）
    static const QColor colors[] = {
        QColor(0x00, 0x00, 0xFF), // Blue
        QColor(0xFF, 0x00, 0x00), // Red
        QColor(0x00, 0x80, 0x00), // Green
        QColor(0x00, 0xFF, 0xFF), // Cyan
        QColor(0xFF, 0x00, 0xFF), // Magenta
        QColor(0x80, 0x80, 0x00), // Olive
        QColor(0x00, 0x00, 0x80), // Navy
        QColor(0x80, 0x00, 0x00), // Maroon
        QColor(0x00, 0x80, 0x80), // Teal
        QColor(0xC0, 0x00, 0x00), // Dark Red
        QColor(0x80, 0x80, 0xC0), // Light Navy
        QColor(0x80, 0x40, 0x00), // Brown
        QColor(0x60, 0x80, 0x00), // Olive Green
        QColor(0x80, 0x00, 0xFF), // Purple
        QColor(0x00, 0x80, 0xFF), // Sky Blue
        QColor(0xFF, 0x80, 0x00), // Orange
    };
    return colors[index % 16];
}

bool GraphicView::isLightTheme()
{
    return true;  // Light-only workbench
}

QString GraphicView::toolbarQss() const
{
    const Theme &t = ThemeManager::instance()->currentTheme();
    return QString(
        "QToolBar { background: %1; border: none; border-bottom: 1px solid %2; spacing: 2px; padding: 2px; }"
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; "
        "padding: 1px 2px; color: %3; font-size: 13px; }"
        "QToolButton:hover { background: %4; border-color: %5; }"
        "QToolButton:checked { background: %6; border-color: %7; color: #fff; }"
        "QCheckBox { color: %3; font-size: 12px; padding: 2px 4px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid %5; "
        "border-radius: 3px; background: transparent; }"
        "QCheckBox::indicator:checked { background: %6; border-color: %7; "
        "image: url(:/icons/check.svg); }"
        "QComboBox { background: %4; border: 1px solid %5; border-radius: 3px; "
        "padding: 2px 6px; color: %3; font-size: 12px; min-width: 56px; min-height: 20px; }"
        "QComboBox:hover { border-color: %5; }"
        "QComboBox QAbstractItemView { background: %1; border: 1px solid %5; "
        "selection-background-color: %6; color: %3; }"
        "QLabel { color: %8; font-size: 12px; padding-left: 4px; }"
    )
    .arg(t.panelBg, t.border, t.text, t.buttonBg, t.border, t.accent, t.accentBorder, t.textDim);
}

QString GraphicView::treeQss() const
{
    const Theme &t = ThemeManager::instance()->currentTheme();
    return QString(
        "QTreeWidget { background: %1; color: %2; border: 1px solid %7; "
        "border-radius: 4px; font-size: 12px; }"
        "QTreeWidget::item { padding: 3px 4px; min-height: 22px; border-radius: 4px; }"
        "QTreeWidget::item:hover { background: %8; }"
        "QTreeWidget::item:alternate { background: %3; }"
        "QTreeWidget::item:selected { background: %4; color: %2; }"
        "QHeaderView::section { background: %5; color: %6; border: none; "
        "border-bottom: 1px solid %7; padding: 3px 4px; font-size: 11px; }"
    )
    .arg(t.contentBg, t.text, t.altRowBg, t.selectionBg, t.headerBg, t.textDim, t.border,
         t.hoverBg);
}

QString GraphicView::infoLabelQss() const
{
    const Theme &t = ThemeManager::instance()->currentTheme();
    return QString(
        "QLabel { padding: 4px 8px; background: %1; color: %2; "
        "border-top: 1px solid %3; font-family: Consolas, monospace; font-size: 12px; }"
    )
    .arg(t.panelBg, t.text, t.border);
}

void GraphicView::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    const Theme &th = ThemeManager::instance()->currentTheme();
    auto buttonQss = [&th](const QString &bg, const QString &border) {
        return QString("QPushButton { background: %1; color: %2; border: 1px solid %3; "
                       "border-radius: 3px; padding: 4px 8px; font-size: 12px; }")
               .arg(bg, th.text, border);
    };

    // ---- 工具栏（对标 CANoe 分组，主题化 QSS） ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setStyleSheet(toolbarQss());

    auto makeBtn = [this](const QString &iconName, const QString &tip) -> QToolButton* {
        auto *btn = new QToolButton(m_toolbar);
        applyToolBarIcon(btn, iconName);
        btn->setIconSize(QSize(16, 16));
        btn->setToolTip(tip);
        btn->setAutoRaise(true);
        // 统一尺寸（§10.5）：固定 28×24 图标钮（VS Code 线条 SVG，见 icons/*.svg）
        btn->setFixedSize(28, 24);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        return btn;
    };

    // 暂停/继续
    m_pauseBtn = makeBtn("pause", "暂停/继续采集 (Space)");
    m_pauseBtn->setCheckable(true);

    m_zoomInBtn = makeBtn("zoom-in", "放大 (+)");
    m_zoomOutBtn = makeBtn("zoom-out", "缩小 (-)");
    m_fitBtn = makeBtn("fit", "适应窗口 (F)");
    m_undoZoomBtn = makeBtn("undo", "撤销缩放 (Ctrl+Z)");
    m_undoZoomBtn->setEnabled(false);
    m_timeBackBtn = makeBtn("chevron-left", "时间窗后移 (←，按住连续)");
    m_timeFwdBtn = makeBtn("chevron-right", "时间窗前移 (→，按住连续)");
    m_yUpBtn = makeBtn("chevron-up", "选中信号 Y 轴上移（按住连续）");
    m_yDownBtn = makeBtn("chevron-down", "选中信号 Y 轴下移（按住连续）");
    m_yUpBtn->setEnabled(false);   // 初始无选中信号（随 setSelectedSignal 联动）
    m_yDownBtn->setEnabled(false);
    for (auto *b : {m_timeBackBtn, m_timeFwdBtn, m_yUpBtn, m_yDownBtn}) {
        b->setAutoRepeat(true);
        b->setAutoRepeatDelay(300);
        b->setAutoRepeatInterval(60);
    }
    m_rubberZoomBtn = makeBtn("rubber-zoom", "框选缩放（左键拖框放大，扁平框仅 X）");
    m_rubberZoomBtn->setCheckable(true);
    m_rubberZoomBtn->setChecked(m_rubberZoom);
    auto *clearDataBtn = makeBtn("clear-all", "清空全部信号数据");
    auto *exportBtn = makeBtn("save-image", "导出为图片 (PNG)");

    // 缩放轴模式（对标 CANoe X/Y/XY 独立缩放）
    m_zoomAxisCombo = new QComboBox(m_toolbar);
    m_zoomAxisCombo->setToolTip("缩放轴模式（滚轮/框选/±受其约束）");
    m_zoomAxisCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_zoomAxisCombo->addItem("XY");
    m_zoomAxisCombo->addItem("仅X");
    m_zoomAxisCombo->addItem("仅Y");

    // 时间窗口选择
    m_timeWindowCombo = new QComboBox(m_toolbar);
    m_timeWindowCombo->setToolTip("时间窗口宽度");
    m_timeWindowCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    for (int sec : {1, 2, 5, 10, 30, 60, 120, 300, 600})
        m_timeWindowCombo->addItem(QString("%1s").arg(sec), sec);
    m_timeWindowCombo->setCurrentIndex(4); // 默认 30s

    // 曲线显示模式（折线/阶梯/仅点）
    m_displayModeCombo = new QComboBox(m_toolbar);
    m_displayModeCombo->setToolTip("曲线显示模式：折线/阶梯/仅点");
    m_displayModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_displayModeCombo->addItem("折线");
    m_displayModeCombo->addItem("阶梯");
    m_displayModeCombo->addItem("仅点");
    m_displayModeCombo->setCurrentIndex(static_cast<int>(m_displayMode));

    // 聚焦模式（对标 CANoe 全部彩色/选中彩色/仅选中显示）
    m_focusCombo = new QComboBox(m_toolbar);
    m_focusCombo->setToolTip("显示模式：全部彩色 / 选中彩色 / 仅显示选中");
    m_focusCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_focusCombo->addItem("全部彩色");
    m_focusCombo->addItem("选中彩色");
    m_focusCombo->addItem("仅选中");

    // Y-axis layout (CANoe-style three modes); P1-3 default = OverlaySelected
    m_yAxisModeCombo = new QComboBox(m_toolbar);
    m_yAxisModeCombo->setToolTip(QStringLiteral("Y-axis layout: separate / overlay"));
    m_yAxisModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_yAxisModeCombo->addItem(QStringLiteral("Separate"));
    m_yAxisModeCombo->addItem(QStringLiteral("Overlay · selected"));
    m_yAxisModeCombo->addItem(QStringLiteral("Overlay · all"));
    m_yAxisModeCombo->setCurrentIndex(1);

    m_pointsToggle = new QCheckBox("采样点", m_toolbar);
    m_pointsToggle->setToolTip("显示/隐藏采样点");
    m_pointsToggle->setChecked(m_showPoints);

    m_cursorSingleBtn = makeBtn("cursor-single", "Single ruler (C): click plot to place a full-height dashed line");
    m_cursorSingleBtn->setCheckable(true);
    m_cursorDoubleBtn = makeBtn("cursor-double", "Double ruler (V): click twice to place two full-height dashed lines");
    m_cursorDoubleBtn->setCheckable(true);
    m_cursorClearBtn = makeBtn("close", "Clear rulers (Esc)");

    m_cursorLinkToggle = new QCheckBox("联动", m_toolbar);
    m_cursorLinkToggle->setToolTip("多视图游标联动");
    m_cursorLinkToggle->setChecked(m_cursorLink);

    // 分组排列（§10.5：所有下拉带前缀标签 + tooltip，分隔符分组，全部图标钮）：
    // [暂停] | [适应 X/Y 自适应 缩放± 撤销 ◀▶▲▼] | [框选] | [缩放:] | [窗口:] [模式:] [显示:] [采样点] | [Y 轴:] | [卡尺 联动] | [清空 导图]
    m_toolbar->addWidget(m_pauseBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_fitBtn);
    // G13：手形拖动（左键平移，与框选缩放互斥）+ X/Y 轴自适应
    m_panBtn = makeBtn("hand", "手形拖动：按住左键平移波形（再次点击关闭）");
    m_panBtn->setCheckable(true);
    connect(m_panBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_panMode = checked;
        if (checked && m_rubberZoomBtn->isChecked())
            m_rubberZoomBtn->setChecked(false);   // 互斥：手形开 → 框选关
        m_plot->setCursor(checked ? Qt::OpenHandCursor : Qt::ArrowCursor);
    });
    auto *fitXBtn = makeBtn("axis-fit-x", "X 轴自适应：时间范围适配全部数据");
    connect(fitXBtn, &QToolButton::clicked, this, [this]() { fitXOnly(); });
    auto *fitYBtn = makeBtn("axis-fit-y", "Y 轴自适应：全部信号 Y 轴适配数据范围");
    connect(fitYBtn, &QToolButton::clicked, this, [this]() { fitYOnly(); });
    m_toolbar->addWidget(m_panBtn);
    m_toolbar->addWidget(fitXBtn);
    m_toolbar->addWidget(fitYBtn);
    m_toolbar->addWidget(m_zoomInBtn);
    m_toolbar->addWidget(m_zoomOutBtn);
    m_toolbar->addWidget(m_undoZoomBtn);
    m_toolbar->addWidget(m_timeBackBtn);
    m_toolbar->addWidget(m_timeFwdBtn);
    m_toolbar->addWidget(m_yUpBtn);
    m_toolbar->addWidget(m_yDownBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_rubberZoomBtn);
    m_toolbar->addSeparator();
    auto *zaLabel = new QLabel("缩放:", m_toolbar);
    zaLabel->setToolTip(m_zoomAxisCombo->toolTip());
    m_toolbar->addWidget(zaLabel);
    m_toolbar->addWidget(m_zoomAxisCombo);
    m_toolbar->addSeparator();
    auto *twLabel = new QLabel("窗口:", m_toolbar);
    twLabel->setToolTip(m_timeWindowCombo->toolTip());
    m_toolbar->addWidget(twLabel);
    m_toolbar->addWidget(m_timeWindowCombo);
    auto *dmLabel = new QLabel("模式:", m_toolbar);
    dmLabel->setToolTip(m_displayModeCombo->toolTip());
    m_toolbar->addWidget(dmLabel);
    m_toolbar->addWidget(m_displayModeCombo);
    auto *fcLabel = new QLabel("显示:", m_toolbar);
    fcLabel->setToolTip(m_focusCombo->toolTip());
    m_toolbar->addWidget(fcLabel);
    m_toolbar->addWidget(m_focusCombo);
    m_toolbar->addWidget(m_pointsToggle);
    m_toolbar->addSeparator();
    auto *yaLabel = new QLabel(QStringLiteral("Y:"), m_toolbar);
    yaLabel->setToolTip(m_yAxisModeCombo->toolTip());
    m_toolbar->addWidget(yaLabel);
    m_toolbar->addWidget(m_yAxisModeCombo);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_cursorSingleBtn);
    m_toolbar->addWidget(m_cursorDoubleBtn);
    m_toolbar->addWidget(m_cursorClearBtn);
    m_toolbar->addWidget(m_cursorLinkToggle);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(clearDataBtn);
    m_toolbar->addWidget(exportBtn);
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    mainLayout->addWidget(m_toolbar);

    // ---- 分割器: 信号列表 | 波形区 ----
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧: 信号列表 (QTreeWidget，首列色块，对标 CANoe)
    auto *leftWidget = new QWidget(m_splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_signalTree = new QTreeWidget(leftWidget);
    m_signalTree->setColumnCount(9);
    m_signalTree->setHeaderLabels({"", "信号", "物理值", "原始值", "单位", "Min", "Max", "ID", "点数"});
    m_signalTree->setRootIsDecorated(false);
    m_signalTree->setAlternatingRowColors(true);
    m_signalTree->setMinimumWidth(320);
    m_signalTree->setSelectionMode(QAbstractItemView::ExtendedSelection);   // G13：Ctrl/Shift 多选
    m_signalTree->setDragEnabled(true);
    m_signalTree->setAcceptDrops(true);
    m_signalTree->setDropIndicatorShown(true);
    m_signalTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_signalTree->setDefaultDropAction(Qt::MoveAction);
    m_signalTree->setStyleSheet(treeQss());
    m_signalTree->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_signalTree->header()->resizeSection(0, 22);
    m_signalTree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_signalTree->header()->resizeSection(1, 120);
    // 其余列（2-8）：Interactive，支持拖动，合理初始宽度
    for (int c = 2; c < 9; ++c) {
        m_signalTree->header()->setSectionResizeMode(c, QHeaderView::Interactive);
        static const int defaultWidth[] = {0, 0, 80, 60, 50, 60, 60, 60, 50};
        m_signalTree->header()->resizeSection(c, defaultWidth[c]);
    }
    leftLayout->addWidget(m_signalTree, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    btnBar->setSpacing(4);
    auto *addBtn = new QPushButton(
            svgIcon(":/icons/plus.svg", th.text, 14), QStringLiteral("Add Signal"), leftWidget);
    addBtn->setStyleSheet(buttonQss(th.buttonBg, th.accentBorder));
    auto *removeBtn = new QPushButton(
            svgIcon(":/icons/dash.svg", th.text, 14), QStringLiteral("Delete Signal"), leftWidget);
    removeBtn->setStyleSheet(buttonQss(th.buttonBg, th.border));
    // 主题切换 → 重刷按钮图标颜色
    // DEF-08 字符串信号（同上）
    auto *signalBtnRelay = new SignalRelay(leftWidget);
    signalBtnRelay->fire0 = [addBtn, removeBtn]() {
        const QString &c = ThemeManager::instance()->currentTheme().text;
        addBtn->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        removeBtn->setIcon(svgIcon(":/icons/dash.svg", c, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            signalBtnRelay, SLOT(fire()));
    btnBar->addWidget(addBtn);
    btnBar->addWidget(removeBtn);
    leftLayout->addLayout(btnBar);

    // 右侧: QCustomPlot (多轴堆叠)
    auto *cursorPlot = new CursorPlot(m_splitter);
    m_plot = cursorPlot;
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setSelectionRectMode(QCP::srmNone);
    if (AppConfig::instance()->getBool(QStringLiteral("graphic.antialiasing"), true))
        m_plot->setAntialiasedElements(QCP::aeAxes | QCP::aeGrid | QCP::aeZeroLine);
    else
        m_plot->setNotAntialiasedElements(QCP::aeAll);
    // 清除默认 axisRect，后面按信号数量动态创建
    m_plot->plotLayout()->clear();

    // 画布背景（随主题：浅色 = CANoe 白底）
    m_plot->setBackground(m_palette.canvas);

    // 框选缩放橡皮筋
    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, m_plot);

    m_splitter->addWidget(leftWidget);
    m_splitter->addWidget(m_plot);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 700});

    // ---- Cursor info (bottom; rulers only) ----
    m_cursorInfoLabel = new QLabel(this);
    m_cursorInfoLabel->setObjectName("CursorInfoLabel");
    m_cursorInfoLabel->setStyleSheet(infoLabelQss());
    m_cursorInfoLabel->setVisible(false);

    // Local status strip removed — summary goes to the shell status bar.
    m_statusLabel = nullptr;

    mainLayout->addWidget(m_splitter, 1);
    mainLayout->addWidget(m_cursorInfoLabel);

    // ---- G15 P3/P4: 信号列表列配置（右键菜单）----
    connect(m_signalTree->header(), &QHeaderView::customContextMenuRequested,
            this, [this](const QPoint &pos) { showColumnVisibilityMenu(pos); });

    // G15 P4: 恢复上次保存的列配置
    restoreColumnConfig();

    // ---- 快捷键（对标 CANoe 常用操作，当前视图不可见时忽略） ----
    auto addShortcut = [this](const QKeySequence &key, auto &&fn) {
        auto *sc = new QShortcut(key, this);
        connect(sc, &QShortcut::activated, this, [this, fn]() {
            if (isVisible()) fn();
        });
    };
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), [this]() {
        if (m_signalTree->hasFocus())
            m_signalTree->selectAll();
    });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), [this]() {
        if (m_signalTree->hasFocus())
            invertSignalSelection();
    });
    addShortcut(QKeySequence(Qt::Key_F), [this]() { fitAll(); });
    addShortcut(QKeySequence(Qt::Key_Space), [this]() { m_pauseBtn->click(); });
    addShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), [this]() { undoZoom(); });
    addShortcut(QKeySequence(Qt::Key_Plus), [this]() { zoomAt(0.5, m_plot->rect().center()); });
    addShortcut(QKeySequence(Qt::Key_Minus), [this]() { zoomAt(2.0, m_plot->rect().center()); });
    addShortcut(QKeySequence(Qt::Key_Left), [this]() { shiftTimeAxis(-0.1); });
    addShortcut(QKeySequence(Qt::Key_Right), [this]() { shiftTimeAxis(0.1); });
    addShortcut(QKeySequence(Qt::Key_C), [this]() {
        if (!m_cursorSingleBtn->isChecked())
            m_cursorSingleBtn->click();   // setChecked 不触发 clicked 信号
    });
    addShortcut(QKeySequence(Qt::Key_V), [this]() {
        if (!m_cursorDoubleBtn->isChecked())
            m_cursorDoubleBtn->click();
    });
    addShortcut(QKeySequence(Qt::Key_Escape), [this]() { m_cursorClearBtn->click(); });

    // ---- 信号添加/删除 ----
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        if (!m_dbcManager || m_dbcManager->files().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Warning"),
                                 QStringLiteral("No DBC database loaded — load a file in the Database panel first"));
            return;
        }
        DbcSignalPickerDialog dlg(m_dbcManager, QStringLiteral("Add signals to Graphic"), this);
        if (dlg.exec() == QDialog::Accepted) {
            const auto picked = dlg.pickedSignals();
            for (const auto &p : picked) {
                Signal sig;
                sig.name = p.signal.name;
                sig.canId = p.canId;
                sig.extended = p.extended;
                sig.dbcSig = p.signal;
                sig.color = autoColor(m_signals.size());
                addSignal(sig);
            }
            updateStatusBar();
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        if (removeSelectedSignals() <= 0)
            return;
    });

    // ---- 缩放（受缩放轴模式约束，改变前记录缩放历史） ----
    connect(m_zoomInBtn, &QToolButton::clicked, this, [this]() {
        pushZoomState();
        zoomAt(0.5, m_plot->rect().center());
    });
    connect(m_zoomOutBtn, &QToolButton::clicked, this, [this]() {
        pushZoomState();
        zoomAt(2.0, m_plot->rect().center());
    });
    connect(m_fitBtn, &QToolButton::clicked, this, [this]() { fitAll(); });
    connect(m_undoZoomBtn, &QToolButton::clicked, this, [this]() { undoZoom(); });
    connect(m_timeBackBtn, &QToolButton::clicked, this, [this]() { shiftTimeAxis(-0.1); });
    connect(m_timeFwdBtn, &QToolButton::clicked, this, [this]() { shiftTimeAxis(0.1); });
    connect(m_yUpBtn, &QToolButton::clicked, this, [this]() { shiftYAxis(+0.1); });
    connect(m_yDownBtn, &QToolButton::clicked, this, [this]() { shiftYAxis(-0.1); });
    connect(m_rubberZoomBtn, &QToolButton::toggled, this, [this](bool on) {
        m_rubberZoom = on;
        if (on && m_panBtn->isChecked())
            m_panBtn->setChecked(false);   // 互斥：框选开 → 手形关
        // 框选开时左键不再交给 QCP 拖拽（由自绘橡皮筋接管）；关时恢复左键平移
        m_plot->setInteractions(on ? (QCP::iRangeZoom)
                                   : (QCP::iRangeDrag | QCP::iRangeZoom));
    });
    m_plot->setInteractions(QCP::iRangeZoom);  // 默认框选开：左键归橡皮筋
    connect(m_zoomAxisCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        setZoomAxisMode(idx == 0 ? ZoomAxisMode::XY
                      : idx == 1 ? ZoomAxisMode::XOnly
                                 : ZoomAxisMode::YOnly);
    });
    connect(clearDataBtn, &QToolButton::clicked, this, [this]() { clearData(); });
    connect(exportBtn, &QToolButton::clicked, this, [this]() { exportPlot(); });

    // ---- 曲线显示模式 ----
    connect(m_displayModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_displayMode = static_cast<DisplayMode>(idx);
        for (auto &sd : m_signals)
            sd.config.displayMode = idx;
        applyDisplayModeAll();
        m_plot->replot();
    });

    // ---- 聚焦模式（全部彩色/选中彩色/仅选中） ----
    connect(m_focusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_focusMode = idx == 1 ? FocusMode::SelectedColor
                   : idx == 2 ? FocusMode::SelectedOnly
                              : FocusMode::AllColor;
        applyFocus();
        m_plot->replot();
    });

    // ---- Y 轴显示方式（分栏/叠加·选中轴/叠加·全部轴） ----
    connect(m_yAxisModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        m_yAxisModeUserLocked = true;  // B7: manual choice wins over auto-overlay
        m_yAxisMode = idx == 1 ? YAxisMode::OverlaySelected
                   : idx == 2 ? YAxisMode::OverlayAll
                              : YAxisMode::Separate;
        applyYAxisMode();
    });

    // ---- 暂停/继续 ----
    connect(m_pauseBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_paused = checked;
        applyToolBarIcon(m_pauseBtn,
                         checked ? QStringLiteral("play") : QStringLiteral("pause"));
        m_pauseBtn->setToolTip(checked ? "继续采集" : "暂停采集");
        updateStatusBar();
    });

    // ---- 时间窗口（改变前记录缩放历史） ----
    connect(m_timeWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        int sec = m_timeWindowCombo->itemData(idx).toInt();
        if (sec > 0) {
            pushZoomState();
            m_timeWindow = sec;
            refreshTimeAxis();
            updateStatusBar();
        }
    });

    // ---- 信号列表选中 → 叠加选中轴切换 + 聚焦刷新 ----
    connect(m_signalTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
        setSelectedSignal(cur ? m_signalTree->indexOfTopLevelItem(cur) : -1);
    });

    // Drag-reorder: after InternalMove, sync m_signals order to the tree
    connect(m_signalTree->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int /*start*/, int /*end*/,
                   const QModelIndex &, int /*dest*/) {
        if (m_signalTree->topLevelItemCount() != m_signals.size())
            return;
        QVector<int> order;
        order.reserve(m_signals.size());
        for (int i = 0; i < m_signalTree->topLevelItemCount(); ++i) {
            const int oldIdx =
                m_signalTree->topLevelItem(i)->data(0, Qt::UserRole).toInt();
            if (oldIdx < 0 || oldIdx >= m_signals.size())
                return;
            order.append(oldIdx);
        }
        // Identity permutation — nothing to do
        bool changed = false;
        for (int i = 0; i < order.size(); ++i) {
            if (order[i] != i) {
                changed = true;
                break;
            }
        }
        if (!changed)
            return;

        QVector<SignalData> reordered;
        reordered.reserve(m_signals.size());
        int newSel = -1;
        for (int i = 0; i < order.size(); ++i) {
            if (order[i] == m_selectedSignal)
                newSel = i;
            reordered.append(std::move(m_signals[order[i]]));
        }
        m_signals = std::move(reordered);
        m_selectedSignal = newSel;
        for (int i = 0; i < m_signalTree->topLevelItemCount(); ++i)
            m_signalTree->topLevelItem(i)->setData(0, Qt::UserRole, i);
        rebuildIdIndex();
        m_zoomStack.clear();
        updateZoomUi();
        if (m_yAxisMode != YAxisMode::Separate)
            applyYAxisMode();
        else
            layoutAxisRects();
        applyFocus();
        syncTreeSelectionToSelected();
        m_plot->replot();
    });

    // ---- Sample markers (rebuild display so zoom-in uses true samples) ----
    connect(m_pointsToggle, &QCheckBox::toggled, this, [this](bool on) {
        m_showPoints = on;
        for (auto &sd : m_signals)
            sd.displayDirty = true;
        applyDisplayModeAll();
        refreshDisplayData();
        m_plot->replot();
    });

    // ---- 卡尺按钮 ----
    connect(m_cursorSingleBtn, &QToolButton::clicked, this, [this]() {
        if (m_cursorSingleBtn->isChecked()) {
            m_cursorDoubleBtn->setChecked(false);
            setCursorMode(CursorMode::Single);
        } else {
            setCursorMode(CursorMode::None);
        }
    });
    connect(m_cursorDoubleBtn, &QToolButton::clicked, this, [this]() {
        if (m_cursorDoubleBtn->isChecked()) {
            m_cursorSingleBtn->setChecked(false);
            setCursorMode(CursorMode::Double);
        } else {
            setCursorMode(CursorMode::None);
        }
    });
    connect(m_cursorClearBtn, &QToolButton::clicked, this, [this]() {
        m_cursorSingleBtn->setChecked(false);
        m_cursorDoubleBtn->setChecked(false);
        setCursorMode(CursorMode::None);
    });

    // ---- 多视图游标联动 ----
    connect(m_cursorLinkToggle, &QCheckBox::toggled, this, [this](bool on) {
        m_cursorLink = on;
    });

    // ---- 信号列表右键菜单（对标 CANoe 信号操作 + 轴快捷操作） ----
    m_signalTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_signalTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_signalTree->itemAt(pos);
        if (!item) return;
        int row = m_signalTree->indexOfTopLevelItem(item);
        QMenu menu(this);  // 样式随全局主题 QSS
        auto *colorAction = menu.addAction("更改颜色...");
        menu.addSeparator();
        auto *fitAction = menu.addAction("Y 轴适应");
        auto *dbcAction = menu.addAction("Y 轴重置为 DBC 范围");
        auto *axisAction = menu.addAction("Y 轴设置...");
        auto *clrAction = menu.addAction("清空数据");
        menu.addSeparator();
        auto *rmAction = menu.addAction("删除信号");
        menu.addSeparator();
        auto *selectAllAction = menu.addAction("全选 (Ctrl+A)");
        auto *invertAction = menu.addAction("反选 (Ctrl+I)");
        auto *clearSelAction = menu.addAction("取消选择");
        auto *sel = menu.exec(m_signalTree->mapToGlobal(pos));
        if (sel == rmAction) {
            // G13 多选删除：点击行在选集中且选集非单 → 删整个选集；否则仅删点击行
            const QList<QTreeWidgetItem*> selItems = m_signalTree->selectedItems();
            QList<int> rows;
            if (selItems.size() > 1 && item->isSelected()) {
                for (QTreeWidgetItem *it : selItems)
                    rows.append(m_signalTree->indexOfTopLevelItem(it));
            } else {
                rows.append(row);
            }
            std::sort(rows.begin(), rows.end(), [](int a, int b) { return a > b; });
            for (int r : rows)
                if (r >= 0 && r < m_signals.size())
                    removeSignal(r);
        } else if (sel == selectAllAction) {
            m_signalTree->selectAll();
        } else if (sel == invertAction) {
            invertSignalSelection();
        } else if (sel == clearSelAction) {
            m_signalTree->clearSelection();
        } else if (sel == clrAction) {
            if (row >= 0 && row < m_signals.size() && m_signals[row].graph) {
                m_signals[row].graph->data()->clear();
                m_signals[row].rawData.clear();
                m_signals[row].hasMinMax = false;
                m_signals[row].minMaxDirty = false;
                m_signals[row].cacheValid = false;
            }
            m_plot->replot();
        } else if (sel == fitAction) {
            fitSignalY(row);
        } else if (sel == dbcAction) {
            resetSignalYToDbc(row);
        } else if (sel == axisAction) {
            showAxisConfigDialog(row);
        } else if (sel == colorAction) {
            if (row >= 0 && row < m_signals.size()) {
                QColor newColor = QColorDialog::getColor(
                    m_signals[row].config.color, this, "选择信号颜色");
                if (newColor.isValid()) {
                    m_signals[row].config.color = newColor;
                    applyFocus();          // 按 focus 状态重设画笔（原色/置灰）
                    refreshNameLabels();   // 重设色块富文本标签
                    updateSignalList();
                    m_plot->replot();
                }
            }
        }
    });

    // ---- 信号列表 checkbox → show/hide（userHidden + 聚焦状态共同决定可见性） ----
    connect(m_signalTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        int row = m_signalTree->indexOfTopLevelItem(item);
        if (row >= 0 && row < m_signals.size()) {
            m_signals[row].userHidden = (item->checkState(1) != Qt::Checked);
            applyFocus();   // 曲线/轨道可见性 + 置灰统一重算（内含 layoutAxisRects）
            m_plot->replot();
        }
    });

    // ---- Mouse wheel zoom (anchor under cursor stays fixed — CANoe-like) ----
    cursorPlot->onWheel = [this](QWheelEvent *event) {
        // Axis-band hit: X band → X only / Y band → that Y (overrides zoom-axis combo)
        int zoneSig = -1;
        if (const int zone = axisZoneAt(event->position().toPoint(), &zoneSig)) {
            const double factor = (event->angleDelta().y() > 0) ? 0.8 : 1.25;
            if (!m_zoomPushTimer.isActive())
                pushZoomState();
            m_zoomPushTimer.start();
            if (zone == 1) {
                if (QCPAxis *xAxis = primaryXAxis()) {
                    const double anchor = xAxis->pixelToCoord(event->position().x());
                    setXRangeAll(zoomRangeAbout(xAxis->range(), anchor, factor), false);
                }
            } else {
                auto zoomYAxis = [&](QCPAxis *ya) {
                    const double anchor = ya->pixelToCoord(event->position().y());
                    ya->setRange(zoomRangeAbout(ya->range(), anchor, factor));
                };
                if (zoneSig >= 0) {
                    if (QCPAxis *ya = valueAxisFor(m_signals[zoneSig]))
                        zoomYAxis(ya);
                } else if (m_yAxisMode != YAxisMode::Separate) {
                    for (auto &sd : m_signals)
                        if (QCPAxis *ya = valueAxisFor(sd))
                            zoomYAxis(ya);
                }
            }
            refreshDisplayData();
            m_plot->replot();
            event->accept();
            return;
        }

        QPoint pos = event->position().toPoint();
        QCPAxisRect *targetAr = nullptr;
        for (auto &sd : m_signals) {
            if (sd.axisRect && sd.axisRect->visible() && sd.axisRect->rect().contains(pos)) {
                targetAr = sd.axisRect;
                break;
            }
        }
        if (!targetAr && m_overlayRect && m_overlayRect->visible() &&
            m_overlayRect->rect().contains(pos))
            targetAr = m_overlayRect;
        if (!targetAr) return;

        const double factor = (event->angleDelta().y() > 0) ? 0.8 : 1.25;
        QCPAxis *xAxis = targetAr->axis(QCPAxis::atBottom);

        if (!m_zoomPushTimer.isActive())
            pushZoomState();
        m_zoomPushTimer.start();

        // X: zoom about mouse time (XOnly / XY)
        if (m_zoomAxis != ZoomAxisMode::YOnly) {
            const double anchor = xAxis->pixelToCoord(pos.x());
            setXRangeAll(zoomRangeAbout(xAxis->range(), anchor, factor), false);
        }

        // Y: zoom about mouse value so the point under the cursor stays put
        if (m_zoomAxis != ZoomAxisMode::XOnly) {
            auto zoomOneY = [&](QCPAxis *ya) {
                if (!ya)
                    return;
                const double anchor = ya->pixelToCoord(pos.y());
                ya->setRange(zoomRangeAbout(ya->range(), anchor, factor));
            };
            if (m_yAxisMode == YAxisMode::Separate) {
                for (auto &sd : m_signals) {
                    if (sd.axisRect == targetAr) {
                        zoomOneY(valueAxisFor(sd));
                        break;
                    }
                }
            } else if (m_yAxisMode == YAxisMode::OverlaySelected
                       && m_selectedSignal >= 0
                       && m_selectedSignal < m_signals.size()) {
                zoomOneY(valueAxisFor(m_signals[m_selectedSignal]));
            } else {
                for (auto &sd : m_signals)
                    zoomOneY(valueAxisFor(sd));
            }
        }

        refreshDisplayData();
        m_plot->replot();
        event->accept();
    };

    // ---- 右键菜单 (波形区，对标 CANoe：Fit/Undo Zoom/框选/缩放轴/线型/聚焦/卡尺/轴操作) ----
    cursorPlot->onContextMenu = [this](QContextMenuEvent *event) {
        const QPoint pos = event->pos();
        QMenu menu(this);
        QCPAxisRect *ar = m_plot->axisRectAt(pos);

        // ---- Y/X 轴刻度区命中 → 轴专属菜单（rect() 为 margins 内绘图区，axisRectAt 按 outerRect 命中） ----
        const bool onYAxis = ar && pos.x() < ar->rect().left();
        const bool onXAxis = ar && pos.y() > ar->rect().bottom() &&
                             pos.x() >= ar->rect().left();
        if (onYAxis || onXAxis) {
            int idx = ar ? signalIndexAtPos(QPoint(ar->rect().center().x(), pos.y())) : -1;
            if (idx < 0)
                idx = m_selectedSignal;
            if (onYAxis && idx >= 0) {
                auto *yFit = menu.addAction("Y 轴适应");
                auto *yDbc = menu.addAction("Y 轴重置为 DBC 范围");
                auto *yCfg = menu.addAction("Y 轴设置...");
                auto *sel = menu.exec(event->globalPos());
                if (sel == yFit) fitSignalY(idx);
                else if (sel == yDbc) resetSignalYToDbc(idx);
                else if (sel == yCfg) showAxisConfigDialog(idx);
            } else {
                auto *xFit = menu.addAction("适应窗口 (F)");
                menu.addSeparator();
                auto *xUndo = menu.addAction("撤销缩放 (Ctrl+Z)");
                xUndo->setEnabled(!m_zoomStack.isEmpty());
                auto *xUndoAll = menu.addAction("撤销全部缩放");
                xUndoAll->setEnabled(!m_zoomStack.isEmpty());
                auto *sel = menu.exec(event->globalPos());
                if (sel == xFit) fitAll();
                else if (sel == xUndo) undoZoom();
                else if (sel == xUndoAll) undoAllZooms();
            }
            return;
        }

        // ---- 绘图区通用菜单 ----
        auto *fitAction = menu.addAction("适应窗口 (F)");
        auto *undoAction = menu.addAction("撤销缩放 (Ctrl+Z)");
        undoAction->setEnabled(!m_zoomStack.isEmpty());
        auto *undoAllAction = menu.addAction("撤销全部缩放");
        undoAllAction->setEnabled(!m_zoomStack.isEmpty());
        menu.addSeparator();

        auto *rubberAction = menu.addAction("框选缩放");
        rubberAction->setCheckable(true);
        rubberAction->setChecked(m_rubberZoom);

        auto *zoomAxisMenu = menu.addMenu("缩放轴");
        auto *zaGroup = new QActionGroup(zoomAxisMenu);
        auto mkZa = [&](const QString &text, ZoomAxisMode m) {
            auto *a = zaGroup->addAction(text);
            a->setCheckable(true);
            a->setChecked(m_zoomAxis == m);
            zoomAxisMenu->addAction(a);
            return a;
        };
        QAction *zaXY = mkZa("XY", ZoomAxisMode::XY);
        QAction *zaX = mkZa("仅 X", ZoomAxisMode::XOnly);
        QAction *zaY = mkZa("仅 Y", ZoomAxisMode::YOnly);

        auto *dmMenu = menu.addMenu("线型");
        auto *dmGroup = new QActionGroup(dmMenu);
        auto mkDm = [&](const QString &text, DisplayMode m) {
            auto *a = dmGroup->addAction(text);
            a->setCheckable(true);
            a->setChecked(m_displayMode == m);
            dmMenu->addAction(a);
            return a;
        };
        QAction *dmLinear = mkDm("折线", DisplayMode::Linear);
        QAction *dmStep = mkDm("阶梯", DisplayMode::Step);
        QAction *dmPoints = mkDm("仅点", DisplayMode::Points);

        auto *focusMenu = menu.addMenu("显示模式");
        auto *focusGroup = new QActionGroup(focusMenu);
        auto mkFm = [&](const QString &text, FocusMode m) {
            auto *a = focusGroup->addAction(text);
            a->setCheckable(true);
            a->setChecked(m_focusMode == m);
            focusMenu->addAction(a);
            return a;
        };
        QAction *fmAll = mkFm("全部彩色", FocusMode::AllColor);
        QAction *fmSel = mkFm("选中彩色", FocusMode::SelectedColor);
        QAction *fmOnly = mkFm("仅选中", FocusMode::SelectedOnly);

        auto *togglePoints = menu.addAction(m_showPoints ? "隐藏采样点" : "显示采样点");
        menu.addSeparator();

        auto *singleCursorAction = menu.addAction("单卡尺 (C)");
        singleCursorAction->setCheckable(true);
        singleCursorAction->setChecked(m_cursorMode == CursorMode::Single);
        auto *doubleCursorAction = menu.addAction("双卡尺 (V)");
        doubleCursorAction->setCheckable(true);
        doubleCursorAction->setChecked(m_cursorMode == CursorMode::Double);
        auto *clearCursorAction = menu.addAction("清除卡尺 (Esc)");
        menu.addSeparator();

        auto *yaMenu = menu.addMenu("Y 轴");
        auto *yaFit = yaMenu->addAction("Y 轴适应");
        auto *yaDbc = yaMenu->addAction("Y 轴重置为 DBC 范围");
        auto *yaCfg = yaMenu->addAction("Y 轴设置...");
        menu.addSeparator();

        auto *exportAction = menu.addAction("导出图片...");

        auto *sel = menu.exec(event->globalPos());
        if (!sel) return;
        if (sel == fitAction) {
            fitAll();
        } else if (sel == undoAction) {
            undoZoom();
        } else if (sel == undoAllAction) {
            undoAllZooms();
        } else if (sel == rubberAction) {
            m_rubberZoomBtn->setChecked(!m_rubberZoom);
        } else if (sel == zaXY) {
            setZoomAxisMode(ZoomAxisMode::XY);
        } else if (sel == zaX) {
            setZoomAxisMode(ZoomAxisMode::XOnly);
        } else if (sel == zaY) {
            setZoomAxisMode(ZoomAxisMode::YOnly);
        } else if (sel == dmLinear) {
            m_displayModeCombo->setCurrentIndex(0);
        } else if (sel == dmStep) {
            m_displayModeCombo->setCurrentIndex(1);
        } else if (sel == dmPoints) {
            m_displayModeCombo->setCurrentIndex(2);
        } else if (sel == fmAll) {
            m_focusCombo->setCurrentIndex(0);
        } else if (sel == fmSel) {
            m_focusCombo->setCurrentIndex(1);
        } else if (sel == fmOnly) {
            m_focusCombo->setCurrentIndex(2);
        } else if (sel == togglePoints) {
            m_pointsToggle->setChecked(!m_showPoints);
        } else if (sel == singleCursorAction) {
            m_cursorSingleBtn->click();
        } else if (sel == doubleCursorAction) {
            m_cursorDoubleBtn->click();
        } else if (sel == clearCursorAction) {
            m_cursorClearBtn->click();
        } else if (sel == yaFit) {
            fitSignalY(m_selectedSignal);
        } else if (sel == yaDbc) {
            resetSignalYToDbc(m_selectedSignal);
        } else if (sel == yaCfg) {
            showAxisConfigDialog(m_selectedSignal);
        } else if (sel == exportAction) {
            exportPlot();
        }
    };

    // ---- 鼠标交互：卡尺拖动(含手柄) / 框选缩放 / 中键平移 / 双击轴操作 / 跟踪线 ----
    cursorPlot->onMousePress = [this](QMouseEvent *event) {
        const QPoint pos = event->pos();

        // 中键 / 手形模式左键：平移启动（X 全局 + Y 按下时所在轨道；G13 手形优先级最高）
        const bool panButton = event->button() == Qt::MiddleButton ||
                               (m_panMode && event->button() == Qt::LeftButton);
        if (panButton) {
            m_panning = true;
            if (m_panMode && event->button() == Qt::LeftButton)
                m_plot->setCursor(Qt::ClosedHandCursor);
            m_panStartPos = pos;
            pushZoomState();
            if (QCPAxis *x = primaryXAxis()) {
                m_panStartX1 = x->range().lower;
                m_panStartX2 = x->range().upper;
            }
            m_panStartY.clear();
            const int panSig = signalIndexAtPos(pos);
            if (panSig >= 0) {
                if (QCPAxis *ya = valueAxisFor(m_signals[panSig]))
                    m_panStartY.append({panSig, ya->range().lower, ya->range().upper});
            } else {
                // 叠加轨道：全部信号 Y 一起平移
                for (int i = 0; i < m_signals.size(); ++i)
                    if (QCPAxis *ya = valueAxisFor(m_signals[i]))
                        m_panStartY.append({i, ya->range().lower, ya->range().upper});
            }
            event->accept();
            return;
        }

        if (event->button() != Qt::LeftButton)
            return;

        // 轴区拖动（§十：X 轴区平移时间 / Y 轴区平移该轴；先于卡尺与橡皮筋）
        int zoneSig = -1;
        const int zone = axisZoneAt(pos, &zoneSig);
        if (zone != 0) {
            m_axisDrag = zone == 1 ? AxisDragMode::X : AxisDragMode::Y;
            m_axisDragSig = zone == 2 ? zoneSig : -1;
            pushZoomState();
            m_panStartPos = pos;
            m_panStartY.clear();
            if (m_axisDrag == AxisDragMode::X) {
                if (QCPAxis *x = primaryXAxis()) {
                    m_panStartX1 = x->range().lower;
                    m_panStartX2 = x->range().upper;
                }
            } else if (m_axisDragSig >= 0) {
                if (QCPAxis *ya = valueAxisFor(m_signals[m_axisDragSig]))
                    m_panStartY.append({m_axisDragSig, ya->range().lower, ya->range().upper});
            } else if (m_yAxisMode != YAxisMode::Separate) {
                // 叠加轴带间隙：全部 Y
                for (int i = 0; i < m_signals.size(); ++i)
                    if (QCPAxis *ya = valueAxisFor(m_signals[i]))
                        m_panStartY.append({i, ya->range().lower, ya->range().upper});
            }
            event->accept();
            return;
        }

        // Cursor hit / place: ruler tool owns left-click on the plot (no rubber-zoom steal)
        if (m_cursorMode != CursorMode::None) {
            QCPAxis *xAxis = primaryXAxis();
            const QRect span = cursorSpanRect();
            if (xAxis) {
                const int px1 = m_cursor1Placed
                    ? static_cast<int>(xAxis->coordToPixel(m_cursor1Time)) : -9999;
                const int px2 = (m_cursorMode == CursorMode::Double && m_cursor2Placed)
                    ? static_cast<int>(xAxis->coordToPixel(m_cursor2Time)) : -9999;
                const bool handleZone = !span.isNull() && span.contains(pos)
                                        && pos.y() <= span.top() + 28;
                auto hit = [&](int px) {
                    return px > -9000 && (std::abs(pos.x() - px) <= 6 ||
                           (handleZone && std::abs(pos.x() - px) <= 10));
                };
                if (m_cursorMode == CursorMode::Double && hit(px2)) {
                    m_draggingCursor = 2;
                    event->accept();
                    return;
                }
                if (hit(px1)) {
                    m_draggingCursor = 1;
                    event->accept();
                    return;
                }
            }
            placeOrMoveCursorAt(pos);
            event->accept();
            return;
        }

        // Waveform click → select matching signal in the list (bidirectional link)
        if (!m_panMode) {
            const int hit = pickSignalAt(pos);
            if (hit >= 0)
                setSelectedSignal(hit);
        }

        // Rubber-band zoom (disabled while ruler placement mode is active)
        if (m_rubberZoom) {
            m_rubberOrigin = pos;
            m_rubberBand->setGeometry(QRect(pos, QSize()));
            m_rubberBand->show();
            event->accept();
        }
    };

    cursorPlot->onMouseMove = [this](QMouseEvent *event) {
        const QPoint pos = event->pos();

        // 0) 轴区拖动平移（§十：X 全轨道同步 / Y 单轴差值）
        if (m_axisDrag != AxisDragMode::None) {
            if (m_axisDrag == AxisDragMode::X) {
                if (QCPAxis *x = primaryXAxis()) {
                    const double t0 = x->pixelToCoord(m_panStartPos.x());
                    const double t1 = x->pixelToCoord(pos.x());
                    setXRangeAll(QCPRange(m_panStartX1 - (t1 - t0),
                                          m_panStartX2 - (t1 - t0)), false);
                }
            } else {
                for (const auto &py : m_panStartY) {
                    if (py.sig < 0 || py.sig >= m_signals.size())
                        continue;
                    if (QCPAxis *ya = valueAxisFor(m_signals[py.sig])) {
                        const double y0 = ya->pixelToCoord(m_panStartPos.y());
                        const double y1 = ya->pixelToCoord(pos.y());
                        ya->setRange(py.lo - (y1 - y0), py.hi - (y1 - y0));
                    }
                }
            }
            refreshDisplayData();
            m_plot->replot(QCustomPlot::rpQueuedReplot);
            event->accept();
            return;
        }

        // 1) 卡尺拖动
        if (m_draggingCursor != 0) {
            if (QCPAxis *xAxis = primaryXAxis()) {
                moveCursor(m_draggingCursor, xAxis->pixelToCoord(pos.x()));
                event->accept();
                return;
            }
        }

        // 2) 框选橡皮筋更新
        if (m_rubberBand->isVisible()) {
            m_rubberBand->setGeometry(QRect(m_rubberOrigin, pos).normalized());
            event->accept();
            return;
        }

        // 3) 中键/手形左键平移
        if (m_panning) {
            // G14 P3b: 手形左键仅做 X 平移（全局同步），中键保持 X+Y 分离平移
            // mouseMove 事件中 button() 返回 NoButton，用 buttons() 检测按住状态
            const bool panOnlyX = m_panMode && (event->buttons() & Qt::LeftButton);
            
            if (QCPAxis *x = primaryXAxis()) {
                const double t0 = x->pixelToCoord(m_panStartPos.x());
                const double t1 = x->pixelToCoord(pos.x());
                setXRangeAll(QCPRange(m_panStartX1 - (t1 - t0),
                                      m_panStartX2 - (t1 - t0)), false);
            }

            // 仅中键拖拽时才做 Y 轴平移（手形左键跳过 Y）
            if (!panOnlyX) {
                for (const auto &py : m_panStartY) {
                    if (py.sig < 0 || py.sig >= m_signals.size())
                        continue;
                    if (QCPAxis *ya = valueAxisFor(m_signals[py.sig])) {
                        const double y0 = ya->pixelToCoord(m_panStartPos.y());
                        const double y1 = ya->pixelToCoord(pos.y());
                        ya->setRange(py.lo - (y1 - y0), py.hi - (y1 - y0));
                    }
                }
            }
            refreshDisplayData();
            m_plot->replot(QCustomPlot::rpQueuedReplot);
            event->accept();
            return;
        }

        // 4) 鼠标跟踪线（竖直点线 + 时间标签，对标 CANoe）
        if (m_trackLine && m_trackLabel) {
            QCPAxisRect *ar = m_plot->axisRectAt(pos);
            if (ar && ar->rect().contains(pos)) {
                if (QCPAxis *xAxis = ar->axis(QCPAxis::atBottom)) {
                    const double t = xAxis->pixelToCoord(pos.x());
                    const double px = xAxis->coordToPixel(t);
                    // Track line spans all visible tracks (CANoe)
                    const QRect rc = cursorSpanRect().isNull() ? ar->rect() : cursorSpanRect();
                    m_trackLine->point1->setType(QCPItemPosition::ptAbsolute);
                    m_trackLine->point2->setType(QCPItemPosition::ptAbsolute);
                    m_trackLine->setClipToAxisRect(false);
                    m_trackLine->point1->setCoords(px, rc.top());
                    m_trackLine->point2->setCoords(px, rc.bottom());
                    m_trackLine->setVisible(true);
                    m_trackLabel->position->setCoords(QPointF(px, rc.bottom() - 4));
                    m_trackLabel->setText(formatTime(t));
                    m_trackLabel->setVisible(true);
                    m_plot->replot(QCustomPlot::rpQueuedReplot);
                }
            } else if (m_trackLine->visible()) {
                m_trackLine->setVisible(false);
                m_trackLabel->setVisible(false);
                m_plot->replot(QCustomPlot::rpQueuedReplot);
            }
        }
    };

    cursorPlot->onMouseRelease = [this](QMouseEvent *event) {
        if (m_panning && (event->button() == Qt::MiddleButton ||
                          (m_panMode && event->button() == Qt::LeftButton))) {
            m_panning = false;
            if (m_panMode)
                m_plot->setCursor(Qt::OpenHandCursor);   // 释放回到手形
            event->accept();
            return;
        }
        if (event->button() != Qt::LeftButton)
            return;

        // 轴区拖动结束（§十）
        if (m_axisDrag != AxisDragMode::None) {
            m_axisDrag = AxisDragMode::None;
            m_axisDragSig = -1;
            event->accept();
            return;
        }

        // 卡尺拖动结束
        if (m_draggingCursor != 0) {
            m_draggingCursor = 0;
            event->accept();
            return;
        }

        // 框选缩放确认
        if (m_rubberBand->isVisible()) {
            m_rubberBand->hide();
            const QRect geo = m_rubberBand->geometry();
            // Click (no drag): keep as no-op when rulers are off (placement handled on press)
            if (geo.width() < 5 && geo.height() < 5) {
                event->accept();
                return;
            }

            QCPAxisRect *ar = m_plot->axisRectAt(geo.center());
            if (ar) {
                QCPAxis *xAxis = ar->axis(QCPAxis::atBottom);
                double x1 = xAxis->pixelToCoord(geo.left());
                double x2 = xAxis->pixelToCoord(geo.right());
                if (x1 > x2) std::swap(x1, x2);
                pushZoomState();
                const bool flat = geo.height() < 5;   // 扁平框 = 仅缩 X（CANoe Drag zoom）
                if (m_zoomAxis != ZoomAxisMode::YOnly && x2 - x1 > 0)
                    setXRangeAll(QCPRange(x1, x2), false);
                if (!flat && m_zoomAxis != ZoomAxisMode::XOnly) {
                    // Y 仅缩所在轨道（叠加轨道缩全部 Y）
                    const int idx = signalIndexAtPos(geo.center());
                    if (idx >= 0) {
                        if (QCPAxis *ya = valueAxisFor(m_signals[idx])) {
                            double y1 = ya->pixelToCoord(geo.top());
                            double y2 = ya->pixelToCoord(geo.bottom());
                            if (y1 > y2) std::swap(y1, y2);
                            if (y2 - y1 > 0) ya->setRange(y1, y2);
                        }
                    } else {
                        for (auto &sd : m_signals) {
                            if (QCPAxis *ya = valueAxisFor(sd)) {
                                double y1 = ya->pixelToCoord(geo.top());
                                double y2 = ya->pixelToCoord(geo.bottom());
                                if (y1 > y2) std::swap(y1, y2);
                                if (y2 - y1 > 0) ya->setRange(y1, y2);
                            }
                        }
                    }
                }
                refreshDisplayData();
                m_plot->replot();
            }
            event->accept();
        }
    };

    // 双击：Y 刻度区 → 轴设置对话框；轨道内 → 该轨道 Y 适应（对标 CANoe）
    cursorPlot->onMouseDoubleClick = [this](QMouseEvent *event) {
        const QPoint pos = event->pos();
        QCPAxisRect *ar = m_plot->axisRectAt(pos);
        if (!ar)
            return;
        if (pos.x() < ar->rect().left()) {
            int idx = signalIndexAtPos(QPoint(ar->rect().center().x(), pos.y()));
            if (idx < 0)
                idx = m_selectedSignal;
            if (idx >= 0) {
                showAxisConfigDialog(idx);
                event->accept();
            }
            return;
        }
        const int idx = signalIndexAtPos(pos);
        if (idx >= 0) {
            fitSignalY(idx);
            event->accept();
        }
    };

    // 鼠标离开 → 隐藏跟踪线
    cursorPlot->onLeave = [this]() {
        if (m_trackLine && m_trackLine->visible()) {
            m_trackLine->setVisible(false);
            if (m_trackLabel) m_trackLabel->setVisible(false);
            m_plot->replot(QCustomPlot::rpQueuedReplot);
        }
    };

    // ---- 窗口尺寸变化 → 重建显示数据（目标点数随视口宽度变化） ----
    cursorPlot->onResize = [this](QResizeEvent *) {
        for (auto &sd : m_signals)
            sd.cacheValid = false;
        m_replotPending = true;
        if (!m_replotTimer.isActive())
            m_replotTimer.start();
    };
}

// ============================================================
//  样式配置（palette 化，随主题切换）
// ============================================================

void GraphicView::styleYAxis(QCPAxis *axis)
{
    if (!axis)
        return;
    const QPen axisPen(m_palette.axis, 1);
    axis->setBasePen(axisPen);
    axis->setTickPen(axisPen);
    axis->setSubTickPen(axisPen);
    axis->setTickLabelColor(m_palette.axisText);
    axis->setLabelColor(m_palette.axisText);
    axis->setLabel(QString());   // CANoe: no axis title (name tag on track)
    axis->setTickLabels(true);
    axis->setTicks(true);
    axis->setTickLabelPadding(2);
    axis->grid()->setVisible(true);
    axis->grid()->setPen(QPen(m_palette.grid, 1, Qt::SolidLine));
    axis->grid()->setSubGridVisible(false);
}

void GraphicView::styleAxisRect(QCPAxisRect *ar)
{
    if (!ar)
        return;
    const QPen axisPen(m_palette.axis, 1);

    // 画布：统一底色（不再按信号色染背景 — CANoe 白底黑轴）
    ar->setBackground(QBrush(m_palette.canvas));

    // X 轴（时间，mm:ss.ms 刻度；仅底部轨道显示刻度，由 layoutAxisRects 控制）
    auto *xAxis = ar->axis(QCPAxis::atBottom);
    xAxis->setBasePen(axisPen);
    xAxis->setTickPen(axisPen);
    xAxis->setSubTickPen(axisPen);
    xAxis->setTickLabelColor(m_palette.axisText);
    xAxis->setLabelColor(m_palette.axisText);
    xAxis->setTicker(QSharedPointer<TimeTicker>::create());
    xAxis->setTickLabels(false);
    xAxis->grid()->setVisible(true);
    xAxis->grid()->setPen(QPen(m_palette.grid, 1, Qt::SolidLine));
    xAxis->grid()->setSubGridVisible(false);

    // Y 轴（黑色轴黑字，颜色只属于曲线）
    styleYAxis(ar->axis(QCPAxis::atLeft));

    // 右/顶镜像轴（保留边框，无刻度文字）
    auto *right = ar->axis(QCPAxis::atRight);
    right->setVisible(true);
    right->setTickLabels(false);
    right->setBasePen(axisPen);
    right->setTickPen(axisPen);
    right->setSubTickPen(axisPen);
    auto *top = ar->axis(QCPAxis::atTop);
    top->setVisible(true);
    top->setTickLabels(false);
    top->setBasePen(axisPen);
    top->setTickPen(axisPen);
    top->setSubTickPen(axisPen);

    // Track margins: leave room for Y tick labels (left) and X labels (bottom of last track).
    // Zero margins here would clip tick text — that was why X/Y scales disappeared.
    ar->setAutoMargins(QCP::msNone);
    ar->setMinimumMargins(QMargins(0, 0, 0, 0));
    ar->setMargins(QMargins(56, 0, 24, 0));
}

void GraphicView::updateToolbarIcons()
{
    // 按 iconName 属性重刷（暂停钮的当前图标随切换状态记录）
    const auto buttons = m_toolbar->findChildren<QToolButton *>();
    for (auto *b : buttons) {
        const QString name = b->property("iconName").toString();
        if (!name.isEmpty())
            applyToolBarIcon(b, name);
    }
}

/**
 * @brief 卡尺顶部手柄 — 渲染 SVG 下箭头图片（替代 "▼" 文字符号，
 *        部分字体下文字符号渲染为方块/乱码）
 *
 * 拖拽命中由 GraphicView 按像素距离判定（±10px），项目自身不参与选中。
 */
class CursorHandleItem : public QCPAbstractItem
{
public:
    explicit CursorHandleItem(QCustomPlot *parentPlot, const QColor &color)
        : QCPAbstractItem(parentPlot)
        , position(createPosition("position"))
    {
        position->setType(QCPItemPosition::ptAbsolute);
        setHandleColor(color);
    }

    /// 随主题/调色板刷新箭头颜色
    void setHandleColor(const QColor &color)
    {
        m_pixmap = renderSvgPixmap(":/icons/chevron-down.svg", color.name(), 12);
    }

    double selectTest(const QPointF &, bool, QVariant *) const override
    {
        return -1.0;
    }

    QCPItemPosition *const position;

protected:
    void draw(QCPPainter *painter) override
    {
        if (m_pixmap.isNull())
            return;
        const QPointF pos = position->pixelPosition();
        // 水平居中、顶部对齐（与时间标签同排）
        painter->drawPixmap(QPointF(pos.x() - m_pixmap.width() / 2.0, pos.y()),
                            m_pixmap);
    }

private:
    QPixmap m_pixmap;
};

void GraphicView::applyPalette()
{
    m_palette = GraphicPalette::canoeLight();
    const Theme &th = ThemeManager::instance()->currentTheme();

    m_plot->setBackground(m_palette.canvas);
    m_toolbar->setStyleSheet(toolbarQss());
    updateToolbarIcons();
    m_signalTree->setStyleSheet(treeQss());
    m_cursorInfoLabel->setStyleSheet(infoLabelQss());
    // Local status strip removed — summary goes to shell status bar.

    // 全部轨道 + 叠加轨道重新着色（重置 X 刻度开关后由 layoutAxisRects 恢复）
    for (auto &sd : m_signals) {
        if (sd.axisRect)
            styleAxisRect(sd.axisRect);
        if (sd.overlayYAxis)
            styleYAxis(sd.overlayYAxis);
    }
    if (m_overlayRect)
        styleAxisRect(m_overlayRect);

    // Time line / cursors / track line (cursors: dashed full-height; time line solid)
    if (m_currentTimeLine)
        m_currentTimeLine->setPen(QPen(m_palette.timeLine, 1.5));
    if (m_cursor1)
        m_cursor1->setPen(QPen(m_palette.cursor1, 1.5, Qt::DashLine));
    if (m_cursor2)
        m_cursor2->setPen(QPen(m_palette.cursor2, 1.5, Qt::DashLine));
    if (m_cursor1Handle)
        m_cursor1Handle->setHandleColor(m_palette.cursor1);
    if (m_cursor2Handle)
        m_cursor2Handle->setHandleColor(m_palette.cursor2);
    if (m_trackLine)
        m_trackLine->setPen(QPen(m_palette.trackCursor, 1, Qt::DotLine));
    if (m_trackLabel) {
        m_trackLabel->setColor(m_palette.axisText);
        m_trackLabel->setBrush(QBrush(m_palette.nameTagBg));
        m_trackLabel->setPen(QPen(m_palette.nameTagBorder, 1));
    }

    applyOverlayAxisVisibility();
    applyFocus();          // 曲线画笔/散点按新 palette 重算（含 refreshNameLabels）
    updateCursorDecorations();
    layoutAxisRects();
    m_plot->replot();
}

// ============================================================
//  多轴布局 + Y 轴三模式（分栏 / 叠加·选中轴 / 叠加·全部轴）
// ============================================================

void GraphicView::layoutAxisRects()
{
    auto *layout = m_plot->plotLayout();
    // G15-P1: tighter row spacing (default 5px → 2px)
    layout->setRowSpacing(2);

    QList<QCPLayoutElement*> taken;
    for (int i = layout->elementCount() - 1; i >= 0; --i) {
        if (auto *el = layout->takeAt(i))
            taken.prepend(el);
    }
    layout->simplify();

    int visibleCount = 0;
    QCPAxisRect *lastVisible = nullptr;
    QCPAxisRect *firstVisible = nullptr;
    for (auto &sd : m_signals) {
        if (sd.axisRect && sd.axisRect->visible()) {
            layout->addElement(visibleCount, 0, sd.axisRect);
            if (!firstVisible)
                firstVisible = sd.axisRect;
            lastVisible = sd.axisRect;
            visibleCount++;
        }
    }

    // Overlay mode: single overlay track
    if (m_overlayRect && m_overlayRect->visible() && visibleCount == 0) {
        layout->addElement(0, 0, m_overlayRect);
        firstVisible = lastVisible = m_overlayRect;
        visibleCount = 1;
    }

    for (auto *el : taken) {
        bool stillUsed = (el == m_overlayRect);
        for (auto &sd : m_signals) {
            if (sd.axisRect == el) {
                stillUsed = true;
                break;
            }
        }
        if (!stillUsed)
            delete el;
    }

    if (visibleCount == 0) {
        auto *ar = new QCPAxisRect(m_plot);
        styleAxisRect(ar);
        ar->setMargins(QMargins(56, 2, 24, 22));
        ar->axis(QCPAxis::atBottom)->setTickLabels(true);
        ar->axis(QCPAxis::atLeft)->setTickLabels(true);
        layout->addElement(0, 0, ar);
        m_plot->replot();
        return;
    }

    // CANoe: shared X scale on bottom track only; each track keeps independent Y ticks.
    // Left/right margins for Y labels; bottom margin only on last track for X labels.
    const int leftBase = 56;
    const int rightM = 24;
    int leftM = leftBase;
    if (m_yAxisMode == YAxisMode::OverlayAll && m_overlayRect && m_overlayRect->visible()) {
        int axisCount = 0;
        for (const auto &sd : m_signals)
            if (sd.overlayYAxis)
                ++axisCount;
        if (axisCount > 1)
            leftM = leftBase + (axisCount - 1) * 45;
    }

    auto applyMargins = [&](QCPAxisRect *ar, bool isFirst, bool isLast) {
        if (!ar)
            return;
        const int top = isFirst ? 2 : 0;
        const int bottom = isLast ? 22 : 0;
        ar->setMargins(QMargins(leftM, top, rightM, bottom));
        if (QCPAxis *xa = ar->axis(QCPAxis::atBottom)) {
            xa->setTickLabels(isLast);
            xa->setTicks(true);
            xa->setTickLabelPadding(3);
        }
        if (QCPAxis *ya = ar->axis(QCPAxis::atLeft)) {
            // Separate mode: every track shows its own Y ticks
            if (m_yAxisMode == YAxisMode::Separate) {
                ya->setTickLabels(true);
                ya->setTicks(true);
            }
        }
    };

    for (auto &sd : m_signals) {
        if (!sd.axisRect || !sd.axisRect->visible())
            continue;
        applyMargins(sd.axisRect,
                     sd.axisRect == firstVisible,
                     sd.axisRect == lastVisible);
        if (sd.yAxis) {
            sd.yAxis->setTickLabels(true);
            sd.yAxis->setTicks(true);
        }
    }
    if (m_overlayRect && m_overlayRect->visible()) {
        applyMargins(m_overlayRect, true, true);
        if (m_overlayXAxis) {
            m_overlayXAxis->setTickLabels(true);
            m_overlayXAxis->setTicks(true);
        }
    }

    applyOverlayAxisVisibility();
    updateCursorDecorations();
    m_plot->replot();
}

void GraphicView::applyOverlayAxisVisibility()
{
    if (!m_overlayRect)
        return;
    int axisIdx = 0;
    for (int i = 0; i < m_signals.size(); ++i) {
        QCPAxis *ya = m_signals[i].overlayYAxis;
        if (!ya)
            continue;
        if (m_yAxisMode == YAxisMode::OverlayAll) {
            // 全部 Y 轴并排（offset 递增错开，刻度文字用信号色区分）
            ya->setTickLabels(true);
            ya->setOffset(axisIdx * 45);
            ya->setTickLabelColor(m_signals[i].config.color);
            axisIdx++;
        } else {
            // 仅选中信号轴显示刻度（黑字）
            ya->setTickLabels(i == m_selectedSignal);
            ya->setOffset(0);
            ya->setTickLabelColor(m_palette.axisText);
        }
    }
}

void GraphicView::buildOverlay()
{
    if (m_signals.isEmpty())
        return;

    // 首次进入叠加模式：创建 overlay 轨道
    if (!m_overlayRect) {
        QCPAxis *px = primaryXAxis();   // 先取分栏主轴（m_overlayRect 赋值后 primaryXAxis 即返回 overlay 轴自身）
        m_overlayRect = new QCPAxisRect(m_plot);
        m_overlayXAxis = m_overlayRect->axis(QCPAxis::atBottom);
        styleAxisRect(m_overlayRect);
        connectXAxis(m_overlayXAxis);   // 仅创建时连接（重复进入叠加模式不再连）
        if (px)
            m_overlayXAxis->setRange(px->range());
        else
            m_overlayXAxis->setRange(0, m_timeWindow);
    }
    m_overlayRect->setVisible(true);

    for (int i = 0; i < m_signals.size(); ++i) {
        SignalData &sd = m_signals[i];
        if (!sd.graph)
            continue;
        // 隐藏分栏轨道
        if (sd.axisRect)
            sd.axisRect->setVisible(false);
        // 每信号独立 overlay Y 轴（首信号复用轨道自带 left 轴，其余 addAxis）
        if (!sd.overlayYAxis) {
            if (i == 0)
                sd.overlayYAxis = m_overlayRect->axis(QCPAxis::atLeft);
            else
                sd.overlayYAxis = m_overlayRect->addAxis(QCPAxis::atLeft);
            styleYAxis(sd.overlayYAxis);
            if (sd.yAxis)
                sd.overlayYAxis->setRange(sd.yAxis->range());
        }
        // 曲线迁移到 overlay 轨道（QCPAbstractPlottable 公开接口）
        sd.graph->setKeyAxis(m_overlayXAxis);
        sd.graph->setValueAxis(sd.overlayYAxis);
        // 名字标签迁移 + 竖直堆叠防重叠
        if (sd.nameLabel) {
            sd.nameLabel->position->setAxisRect(m_overlayRect);
            sd.nameLabel->position->setCoords(0.01, 0.02 + i * 0.06);
        }
    }
    applyOverlayAxisVisibility();
    layoutAxisRects();
}

void GraphicView::teardownOverlay()
{
    // 曲线与标签迁回分栏轨道（必须在销毁 overlay 轴之前）
    for (auto &sd : m_signals) {
        if (sd.graph && sd.axisRect) {
            sd.graph->setKeyAxis(sd.axisRect->axis(QCPAxis::atBottom));
            sd.graph->setValueAxis(sd.yAxis);
        }
        if (sd.nameLabel && sd.axisRect) {
            sd.nameLabel->position->setAxisRect(sd.axisRect);
            sd.nameLabel->position->setCoords(0.01, 0.02);
        }
        if (sd.axisRect)
            sd.axisRect->setVisible(!sd.userHidden);
        sd.overlayYAxis = nullptr;   // 轴随 overlay 轨道销毁
    }
    m_overlayXAxis = nullptr;
    if (m_overlayRect) {
        m_plot->plotLayout()->remove(m_overlayRect);   // remove 内部 delete（含其全部轴）
        m_overlayRect = nullptr;
    }
}

void GraphicView::applyYAxisMode()
{
    if (m_yAxisMode == YAxisMode::Separate || m_signals.isEmpty()) {
        teardownOverlay();
        for (auto &sd : m_signals) {
            if (sd.axisRect)
                sd.axisRect->setVisible(!sd.userHidden);
        }
    } else {
        buildOverlay();
    }
    applyFocus();          // 曲线/轨道可见性按聚焦模式重算（内含 layoutAxisRects）
    refreshNameLabels();
    layoutAxisRects();
    refreshDisplayData();
    m_plot->replot();
}

void GraphicView::maybeAutoOverlay()
{
    // B7 / P1-3: separate AxisRects are expensive — prefer overlay above threshold
    // (default mode is already OverlaySelected; this recovers if user is still Separate).
    const int thr = AppConfig::instance()->getInt(
        QStringLiteral("graphic.overlayAutoThreshold"), 2);
    if (thr <= 0 || m_yAxisModeUserLocked)
        return;
    if (m_signals.size() < thr)
        return;
    if (m_yAxisMode != YAxisMode::Separate)
        return;

    m_yAxisMode = YAxisMode::OverlaySelected;
    if (m_yAxisModeCombo) {
        QSignalBlocker blocker(m_yAxisModeCombo);
        m_yAxisModeCombo->setCurrentIndex(1);
    }
    applyYAxisMode();
}

void GraphicView::addSignal(const Signal &sig,
                            const QVector<CanFrame> *history, int historyCount)
{
    SignalData sd;
    sd.config = sig;
    if (!sd.config.color.isValid())
        sd.config.color = autoColor(m_signals.size());
    if (sd.config.displayMode < 0 || sd.config.displayMode > 2)
        sd.config.displayMode = static_cast<int>(m_displayMode);

    // G15-P1: 轨道紧凑布局 + 边距控制
    sd.axisRect = new QCPAxisRect(m_plot);
    styleAxisRect(sd.axisRect);
    sd.yAxis = sd.axisRect->axis(QCPAxis::atLeft);
    styleYAxis(sd.yAxis);

    // X 轴对齐当前主视口（首信号用默认时间窗）
    QCPAxis *xAxis = sd.axisRect->axis(QCPAxis::atBottom);
    if (QCPAxis *px = primaryXAxis())
        xAxis->setRange(px->range());
    else if (m_currentTime > 0.0)
        // 全部轨道隐藏时的兜底：按当前数据时间感知（绝对时间戳文件下
        // 不再错位到 [0, window] 无数据区）
        xAxis->setRange(std::max(0.0, m_currentTime - m_timeWindow), m_currentTime);
    else
        xAxis->setRange(0, m_timeWindow);

    // 默认 Y 轴范围：DBC 范围（无效则 0..1）
    double yMin = sig.dbcSig.minimum;
    double yMax = sig.dbcSig.maximum;
    if (yMax <= yMin) { yMin = 0.0; yMax = 1.0; }
    sd.yAxis->setRange(yMin, yMax);

    // 曲线（1px 细线，颜色只属于曲线）
    sd.graph = m_plot->addGraph(xAxis, sd.yAxis);
    sd.graph->setName(sig.name);
    sd.graph->setPen(QPen(sd.config.color, 1));

    // 信号名标签（轨道左上角，底色随主题）
    sd.nameLabel = new QCPItemText(m_plot);
    sd.nameLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    sd.nameLabel->position->setAxisRect(sd.axisRect);
    sd.nameLabel->position->setCoords(0.01, 0.02);
    sd.nameLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    sd.nameLabel->setBrush(QBrush(m_palette.nameTagBg));
    sd.nameLabel->setPen(QPen(m_palette.nameTagBorder, 1));
    sd.nameLabel->setPadding(QMargins(4, 2, 4, 2));
    QFont labelFont("Consolas", 8);
    sd.nameLabel->setFont(labelFont);

    m_signals.append(sd);
    rebuildIdIndex();

    // 线型/散点（applyDisplayMode 需 graph 就绪，入列后调用）
    applyDisplayMode(m_signals.last());
    refreshNameLabels();
    connectXAxis(xAxis);

    // 叠加模式下新信号直接迁移到 overlay 轨道
    if (m_yAxisMode != YAxisMode::Separate)
        applyYAxisMode();
    else
        layoutAxisRects();

    // Phase B3: subscribe shared store (+ optional offline history backfill)
    subscribeStore(m_signals.last(), history, historyCount);
    pullFromSampleStore();

    SignalData &ns = m_signals.last();
    ensureMinMax(ns);
    if (QCPAxis *ya = valueAxisFor(ns); ya && ns.hasMinMax) {
        double margin = (ns.dataMax - ns.dataMin) * 0.05;
        if (margin <= 0) margin = 1.0;
        ya->setRange(ns.dataMin - margin, ns.dataMax + margin);
    }
    if (!ns.rawData.empty()) {
        const double t0 = ns.rawData.at(0).t;
        const double t1 = ns.rawData.at(ns.rawData.size() - 1).t;
        m_currentTime = std::max(m_currentTime, t1);
        double tStart = t0;
        double tEnd = t1;
        if (tEnd <= tStart)
            tEnd = tStart + 1.0;
        setXRangeAll(QCPRange(tStart, tEnd), false);
    } else if (ns.storeSubscribed) {
        double t0 = 0.0, t1 = 0.0;
        if (SampleStore::instance()->timeRange(ns.storeKey, &t0, &t1)) {
            m_currentTime = std::max(m_currentTime, t1);
            if (t1 <= t0)
                t1 = t0 + 1.0;
            setXRangeAll(QCPRange(t0, t1), false);
        }
    }

    maybeAutoOverlay();

    updateSignalList();
    refreshDisplayData();
    m_plot->replot();
}

void GraphicView::removeSignal(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;

    // Overlay mode: tear down first, rebuild after delete (avoid dangling axes)
    if (m_yAxisMode != YAxisMode::Separate)
        teardownOverlay();

    auto &sd = m_signals[index];
    unsubscribeStore(sd);
    if (sd.nameLabel)
        m_plot->removeItem(sd.nameLabel);
    if (sd.graph)
        m_plot->removeGraph(sd.graph);
    if (sd.axisRect)
        m_plot->plotLayout()->remove(sd.axisRect);   // remove deletes element (incl. axes)

    m_signals.removeAt(index);
    rebuildIdIndex();
    m_zoomStack.clear();   // signal indices changed → zoom history invalid
    updateZoomUi();
    if (m_selectedSignal >= m_signals.size())
        setSelectedSignal(m_signals.isEmpty() ? -1 : m_signals.size() - 1);

    if (m_yAxisMode != YAxisMode::Separate && !m_signals.isEmpty())
        applyYAxisMode();
    else
        layoutAxisRects();
    updateSignalList();
    m_plot->replot();
}

int GraphicView::removeSelectedSignals()
{
    QList<int> rows;
    const QList<QTreeWidgetItem *> sel = m_signalTree->selectedItems();
    for (QTreeWidgetItem *it : sel)
        rows.append(m_signalTree->indexOfTopLevelItem(it));
    if (rows.isEmpty()) {
        const int row = m_signalTree->indexOfTopLevelItem(m_signalTree->currentItem());
        if (row >= 0 && row < m_signals.size())
            rows.append(row);
    }
    if (rows.isEmpty())
        return 0;

    std::sort(rows.begin(), rows.end(), [](int a, int b) { return a > b; });
    int removed = 0;
    for (int row : rows) {
        if (row >= 0 && row < m_signals.size()) {
            removeSignal(row);
            ++removed;
        }
    }
    return removed;
}

void GraphicView::clearSignals()
{
    teardownOverlay();
    for (auto &sd : m_signals) {
        unsubscribeStore(sd);
        if (sd.nameLabel)
            m_plot->removeItem(sd.nameLabel);
        if (sd.graph)
            m_plot->removeGraph(sd.graph);
        if (sd.axisRect) {
            m_plot->plotLayout()->remove(sd.axisRect);   // remove 内部 delete
            sd.axisRect = nullptr;
        }
    }
    m_signals.clear();
    rebuildIdIndex();
    m_zoomStack.clear();
    updateZoomUi();
    setSelectedSignal(-1);
    layoutAxisRects();
    updateSignalList();
    m_plot->replot();
}

// ============================================================
//  坐标辅助（分栏/叠加双模式取轴）
// ============================================================

QCPAxis *GraphicView::valueAxisFor(const SignalData &sd) const
{
    if (m_yAxisMode != YAxisMode::Separate && sd.overlayYAxis)
        return sd.overlayYAxis;
    return sd.yAxis;
}

QCPAxis *GraphicView::primaryXAxis() const
{
    if (m_yAxisMode != YAxisMode::Separate && m_overlayXAxis)
        return m_overlayXAxis;
    for (auto &sd : m_signals)
        if (sd.axisRect && sd.axisRect->visible())
            return sd.axisRect->axis(QCPAxis::atBottom);
    if (m_plot && m_plot->axisRectCount() > 0)
        return m_plot->axisRect(0)->axis(QCPAxis::atBottom);
    return nullptr;
}

QCPAxisRect *GraphicView::primaryRect() const
{
    if (m_yAxisMode != YAxisMode::Separate && m_overlayRect && m_overlayRect->visible())
        return m_overlayRect;
    for (auto &sd : m_signals)
        if (sd.axisRect && sd.axisRect->visible())
            return sd.axisRect;
    return nullptr;
}

void GraphicView::setXRangeAll(const QCPRange &range, bool refresh)
{
    for (auto &sd : m_signals) {
        if (!sd.axisRect)
            continue;
        auto *xa = sd.axisRect->axis(QCPAxis::atBottom);
        QSignalBlocker blocker(xa);
        xa->setRange(range);
    }
    if (m_overlayXAxis) {
        QSignalBlocker blocker(m_overlayXAxis);
        m_overlayXAxis->setRange(range);
    }
    if (refresh)
        refreshDisplayData();
}

void GraphicView::connectXAxis(QCPAxis *xAxis)
{
    if (!xAxis)
        return;
    connect(xAxis, static_cast<void(QCPAxis::*)(const QCPRange&)>(&QCPAxis::rangeChanged),
        this, [this](const QCPRange &range) {
        if (m_restoringZoom)
            return;
        // 全轨道 X 同步（含 overlay X）
        for (auto &s : m_signals) {
            if (!s.axisRect)
                continue;
            auto *xa = s.axisRect->axis(QCPAxis::atBottom);
            if (xa && xa->range() != range) {
                QSignalBlocker blocker(xa);
                xa->setRange(range);
            }
        }
        if (m_overlayXAxis && m_overlayXAxis->range() != range) {
            QSignalBlocker blocker(m_overlayXAxis);
            m_overlayXAxis->setRange(range);
        }
        // 视口变化 → 重建显示数据（50ms 节流，由定时器统一处理）
        m_replotPending = true;
        if (!m_replotTimer.isActive())
            m_replotTimer.start();
        if (m_cursorMode != CursorMode::None)
            updateCursorValues();
    });
}

int GraphicView::signalIndexAtPos(const QPoint &pos) const
{
    for (int i = 0; i < m_signals.size(); ++i) {
        const auto &sd = m_signals[i];
        if (sd.axisRect && sd.axisRect->visible() && sd.axisRect->rect().contains(pos))
            return i;
    }
    return -1;   // 叠加轨道：无分栏命中
}

int GraphicView::signalIndexForAxis(QCPAxis *yAxis) const
{
    if (!yAxis)
        return -1;
    for (int i = 0; i < m_signals.size(); ++i) {
        if (m_signals[i].yAxis == yAxis || m_signals[i].overlayYAxis == yAxis)
            return i;
    }
    return -1;
}

int GraphicView::axisZoneAt(const QPoint &pos, int *outIdx) const
{
    if (outIdx)
        *outIdx = -1;
    QCPAxisRect *ar = m_plot->axisRectAt(pos);
    if (!ar)
        return 0;
    // X 轴刻度区（绘图内区下方、outerRect 内）
    if (pos.y() > ar->rect().bottom())
        return 1;
    // Y 轴刻度区（绘图内区左侧）
    if (pos.x() < ar->rect().left()) {
        int idx = -1;
        if (m_yAxisMode == YAxisMode::Separate) {
            idx = signalIndexAtPos(QPoint(ar->rect().center().x(), pos.y()));
        } else if (ar == m_overlayRect) {
            if (m_yAxisMode == YAxisMode::OverlaySelected) {
                idx = m_selectedSignal;
            } else {
                // 叠加·全部轴：按并排刻度带定位（带宽 45px，间隙 → -1 = 全部）
                const int right = ar->rect().left();
                for (int i = 0; i < m_signals.size(); ++i) {
                    QCPAxis *ya = m_signals[i].overlayYAxis;
                    if (!ya)
                        continue;
                    const int off = static_cast<int>(ya->offset());
                    if (pos.x() <= right - off && pos.x() >= right - off - 45) {
                        idx = i;
                        break;
                    }
                }
            }
        }
        if (outIdx)
            *outIdx = idx;
        return 2;
    }
    return 0;
}

// ============================================================
//  显示模式 / 聚焦三态 / 选中信号 / 名字标签
// ============================================================

void GraphicView::applyDisplayMode(SignalData &sd)
{
    if (!sd.graph)
        return;
    const QColor c = sd.graph->pen().color();   // 已按聚焦状态设置的画笔色
    const auto mode = static_cast<DisplayMode>(sd.config.displayMode);
    const auto scatter = m_showPoints || mode == DisplayMode::Points
        ? QCPScatterStyle(QCPScatterStyle::ssCircle, c, 3)
        : QCPScatterStyle(QCPScatterStyle::ssNone);
    switch (mode) {
    case DisplayMode::Linear:
        sd.graph->setLineStyle(QCPGraph::lsLine);
        sd.graph->setScatterStyle(scatter);
        break;
    case DisplayMode::Points:
        sd.graph->setLineStyle(QCPGraph::lsNone);
        sd.graph->setScatterStyle(
            QCPScatterStyle(QCPScatterStyle::ssCircle, c, 3));
        break;
    case DisplayMode::Step:
    default:
        sd.graph->setLineStyle(QCPGraph::lsStepLeft);
        sd.graph->setScatterStyle(scatter);
        break;
    }
}

void GraphicView::applyDisplayModeAll()
{
    for (auto &sd : m_signals)
        applyDisplayMode(sd);
}

void GraphicView::applyFocus()
{
    for (int i = 0; i < m_signals.size(); ++i) {
        auto &sd = m_signals[i];
        if (!sd.graph)
            continue;
        const bool selected = (i == m_selectedSignal);
        bool visible = !sd.userHidden;
        QColor color = sd.config.color;
        switch (m_focusMode) {
        case FocusMode::SelectedColor:
            if (m_selectedSignal >= 0 && !selected)
                color = m_palette.dimCurve;
            break;
        case FocusMode::SelectedOnly:
            visible = visible && (m_selectedSignal < 0 || selected);
            break;
        case FocusMode::AllColor:
        default:
            break;
        }
        sd.graph->setVisible(visible);
        // Selected waveform: thicker pen for list↔plot linkage
        sd.graph->setPen(QPen(color, selected ? 2.5 : 1.0));
        // Separate mode: collapse tracks for hidden signals
        if (m_yAxisMode == YAxisMode::Separate && sd.axisRect)
            sd.axisRect->setVisible(visible);
    }
    applyDisplayModeAll();   // 散点颜色跟随画笔（含置灰）
    refreshNameLabels();
    if (m_yAxisMode == YAxisMode::Separate)
        layoutAxisRects();
}

void GraphicView::setSelectedSignal(int index)
{
    if (index >= m_signals.size())
        index = m_signals.isEmpty() ? -1 : m_signals.size() - 1;
    if (m_selectedSignal == index) {
        syncTreeSelectionToSelected();
        return;
    }
    m_selectedSignal = index;
    syncTreeSelectionToSelected();
    applyOverlayAxisVisibility();   // OverlaySelected: tick switch
    applyFocus();
    m_yUpBtn->setEnabled(index >= 0);
    m_yDownBtn->setEnabled(index >= 0);
    m_plot->replot();
}

void GraphicView::syncTreeSelectionToSelected()
{
    if (!m_signalTree)
        return;
    const QSignalBlocker blocker(m_signalTree);
    if (m_selectedSignal < 0 || m_selectedSignal >= m_signalTree->topLevelItemCount()) {
        m_signalTree->setCurrentItem(nullptr);
        m_signalTree->clearSelection();
        return;
    }
    QTreeWidgetItem *item = m_signalTree->topLevelItem(m_selectedSignal);
    m_signalTree->clearSelection();
    item->setSelected(true);
    m_signalTree->setCurrentItem(item);
}

void GraphicView::moveSignal(int from, int to)
{
    if (from < 0 || from >= m_signals.size() || to < 0 || to >= m_signals.size()
        || from == to)
        return;
    m_signals.move(from, to);
    if (m_selectedSignal == from)
        m_selectedSignal = to;
    else if (from < m_selectedSignal && to >= m_selectedSignal)
        --m_selectedSignal;
    else if (from > m_selectedSignal && to <= m_selectedSignal)
        ++m_selectedSignal;
    rebuildIdIndex();
    m_zoomStack.clear();
    updateZoomUi();
    if (m_yAxisMode != YAxisMode::Separate)
        applyYAxisMode();
    else
        layoutAxisRects();
    applyFocus();
    updateSignalList();
    syncTreeSelectionToSelected();
    m_plot->replot();
}

int GraphicView::pickSignalAt(const QPoint &pos) const
{
    if (m_yAxisMode == YAxisMode::Separate) {
        const int idx = signalIndexAtPos(pos);
        if (idx >= 0 && idx < m_signals.size()) {
            const auto &sd = m_signals[idx];
            if (sd.graph && sd.graph->visible() && !sd.userHidden)
                return idx;
        }
        return -1;
    }
    // Overlay: nearest graph by pixel distance
    double bestDist = 8.0;
    int best = -1;
    for (int i = 0; i < m_signals.size(); ++i) {
        const auto &sd = m_signals[i];
        if (!sd.graph || !sd.graph->visible() || sd.userHidden)
            continue;
        const double dist = sd.graph->selectTest(pos, false);
        if (dist >= 0.0 && dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

void GraphicView::refreshNameLabels()
{
    // QCPItemText does not render HTML — rich-text labels were drawn as raw markup
    // and stacked over waveforms in overlay mode. Names/colors live in the signal list.
    for (auto &sd : m_signals) {
        if (!sd.nameLabel)
            continue;
        sd.nameLabel->setVisible(false);
    }
}

// ============================================================
//  缩放（轴模式 + 历史栈，对标 CANoe Undo Zoom / Undo All Zooms）
// ============================================================

void GraphicView::setZoomAxisMode(ZoomAxisMode m)
{
    m_zoomAxis = m;
    const int idx = m == ZoomAxisMode::XY ? 0
                  : m == ZoomAxisMode::XOnly ? 1 : 2;
    if (m_zoomAxisCombo && m_zoomAxisCombo->currentIndex() != idx)
        m_zoomAxisCombo->setCurrentIndex(idx);
}

void GraphicView::shiftTimeAxis(double frac)
{
    QCPAxis *x = primaryXAxis();
    if (!x)
        return;
    // 连续平移（按住箭头/连按快捷键）合并为一级缩放历史（同滚轮防抖）
    if (!m_zoomPushTimer.isActive())
        pushZoomState();
    m_zoomPushTimer.start();
    const QCPRange r = x->range();
    const double d = r.size() * frac;
    setXRangeAll(QCPRange(r.lower + d, r.upper + d));   // 默认 refresh=true 刷新视口数据
    m_plot->replot(QCustomPlot::rpQueuedReplot);   // 轴范围变化需显式重绘（setXRangeAll 不 replot）
}

void GraphicView::shiftYAxis(double frac)
{
    // Pan selected signal's Y axis. Positive frac = move waveform up on screen
    // (decrease axis values; QCP left axis has larger values at the top).
    if (m_selectedSignal < 0 || m_selectedSignal >= m_signals.size())
        return;
    QCPAxis *ya = valueAxisFor(m_signals[m_selectedSignal]);
    if (!ya)
        return;
    if (!m_zoomPushTimer.isActive())
        pushZoomState();
    m_zoomPushTimer.start();
    const QCPRange r = ya->range();
    const double d = r.size() * frac;
    ya->setRange(r.lower - d, r.upper - d);
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void GraphicView::zoomAt(double factor, const QPointF &plotPos)
{
    QCPAxis *xAxis = primaryXAxis();
    if (!xAxis)
        return;
    const int px = static_cast<int>(plotPos.x());
    const int py = static_cast<int>(plotPos.y());
    if (m_zoomAxis != ZoomAxisMode::YOnly) {
        const double anchor = xAxis->pixelToCoord(px);
        setXRangeAll(zoomRangeAbout(xAxis->range(), anchor, factor), false);
    }
    if (m_zoomAxis != ZoomAxisMode::XOnly) {
        for (auto &sd : m_signals) {
            QCPAxis *ya = valueAxisFor(sd);
            if (!ya)
                continue;
            const double anchor = ya->pixelToCoord(py);
            ya->setRange(zoomRangeAbout(ya->range(), anchor, factor));
        }
    }
    refreshDisplayData();
    m_plot->replot();
}

QCPRange GraphicView::zoomRangeAbout(const QCPRange &r, double anchor, double factor)
{
    if (factor <= 0.0 || !qIsFinite(factor) || !qIsFinite(anchor))
        return r;
    // Keep the data value under the cursor fixed in pixel space (X and Y).
    const double lo = anchor - (anchor - r.lower) * factor;
    const double hi = anchor + (r.upper - anchor) * factor;
    if (!qIsFinite(lo) || !qIsFinite(hi) || lo == hi)
        return r;
    return QCPRange(lo, hi);
}

GraphicView::ZoomState GraphicView::currentZoomState() const
{
    ZoomState st;
    if (QCPAxis *x = primaryXAxis()) {
        st.x1 = x->range().lower;
        st.x2 = x->range().upper;
    }
    for (int i = 0; i < m_signals.size(); ++i) {
        if (QCPAxis *ya = valueAxisFor(m_signals[i]))
            st.yRanges.append({i, ya->range().lower, ya->range().upper});
    }
    return st;
}

void GraphicView::pushZoomState()
{
    if (m_restoringZoom)
        return;
    const ZoomState cur = currentZoomState();
    // 与栈顶相同（no-op 操作，如未缩放即 fitAll）不入栈
    if (!m_zoomStack.isEmpty()) {
        const ZoomState &top = m_zoomStack.last();
        bool same = top.x1 == cur.x1 && top.x2 == cur.x2 &&
                    top.yRanges.size() == cur.yRanges.size();
        for (int k = 0; same && k < cur.yRanges.size(); ++k)
            same = top.yRanges[k].sig == cur.yRanges[k].sig &&
                   top.yRanges[k].lo == cur.yRanges[k].lo &&
                   top.yRanges[k].hi == cur.yRanges[k].hi;
        if (same)
            return;
    }
    m_zoomStack.append(cur);
    if (m_zoomStack.size() > 50)
        m_zoomStack.removeFirst();
    updateZoomUi();
}

void GraphicView::undoZoom()
{
    if (m_zoomStack.isEmpty())
        return;
    restoreZoomState(m_zoomStack.takeLast());
    updateZoomUi();
}

void GraphicView::undoAllZooms()
{
    if (m_zoomStack.isEmpty())
        return;
    const ZoomState first = m_zoomStack.first();
    m_zoomStack.clear();
    restoreZoomState(first);
    updateZoomUi();
}

void GraphicView::restoreZoomState(const ZoomState &st)
{
    m_restoringZoom = true;
    setXRangeAll(QCPRange(st.x1, st.x2), false);
    for (const auto &yr : st.yRanges) {
        if (yr.sig < 0 || yr.sig >= m_signals.size())
            continue;
        if (QCPAxis *ya = valueAxisFor(m_signals[yr.sig]))
            ya->setRange(yr.lo, yr.hi);
    }
    m_restoringZoom = false;
    m_zoomPushTimer.stop();   // 撤销后不再补 push
    refreshDisplayData();
    m_plot->replot();
}

void GraphicView::updateZoomUi()
{
    if (m_undoZoomBtn)
        m_undoZoomBtn->setEnabled(!m_zoomStack.isEmpty());
}

// ============================================================
//  单信号 Y 轴快捷操作（适应 / 重置 DBC / 范围设置对话框）
// ============================================================

void GraphicView::fitSignalY(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;
    auto &sd = m_signals[index];
    ensureMinMax(sd);
    QCPAxis *ya = valueAxisFor(sd);
    if (!ya)
        return;
    if (sd.hasMinMax) {
        double margin = (sd.dataMax - sd.dataMin) * 0.05;
        if (margin <= 0) margin = 1.0;
        ya->setRange(sd.dataMin - margin, sd.dataMax + margin);
    } else {
        double yMin = sd.config.dbcSig.minimum;
        double yMax = sd.config.dbcSig.maximum;
        if (yMax <= yMin) { yMin = 0.0; yMax = 1.0; }
        ya->setRange(yMin, yMax);
    }
    m_plot->replot();
}

void GraphicView::resetSignalYToDbc(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;
    QCPAxis *ya = valueAxisFor(m_signals[index]);
    if (!ya)
        return;
    double yMin = m_signals[index].config.dbcSig.minimum;
    double yMax = m_signals[index].config.dbcSig.maximum;
    if (yMax <= yMin) { yMin = 0.0; yMax = 1.0; }
    ya->setRange(yMin, yMax);
    m_plot->replot();
}

void GraphicView::showAxisConfigDialog(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;
    auto &sd = m_signals[index];
    QCPAxis *ya = valueAxisFor(sd);
    if (!ya)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(QString("Y 轴设置 — %1").arg(sd.config.name));
    auto *form = new QFormLayout(&dlg);
    auto *minSpin = new QDoubleSpinBox(&dlg);
    auto *maxSpin = new QDoubleSpinBox(&dlg);
    minSpin->setRange(-1e9, 1e9);
    minSpin->setDecimals(3);
    maxSpin->setRange(-1e9, 1e9);
    maxSpin->setDecimals(3);
    minSpin->setValue(ya->range().lower);
    maxSpin->setValue(ya->range().upper);
    form->addRow("最小值:", minSpin);
    form->addRow("最大值:", maxSpin);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted && minSpin->value() < maxSpin->value()) {
        ya->setRange(minSpin->value(), maxSpin->value());
        m_plot->replot();
    }
}

QVector<GraphicView::Signal> GraphicView::signalConfigs() const
{
    QVector<Signal> result;
    for (const auto &sd : m_signals)
        result.append(sd.config);
    return result;
}

int GraphicView::displayedPointCount(int index) const
{
    if (index < 0 || index >= m_signals.size())
        return -1;
    const auto &sd = m_signals.at(index);
    return sd.graph ? sd.graph->data()->size() : 0;
}

int GraphicView::rawSampleCount(int index) const
{
    if (index < 0 || index >= m_signals.size())
        return -1;
    const auto &sd = m_signals.at(index);
    if (sd.storeSubscribed)
        return SampleStore::instance()->sampleCount(sd.storeKey);
    return sd.rawData.size();
}

void GraphicView::loadSignalConfigs(const QVector<Signal> &configs)
{
    clearSignals();
    for (const auto &sig : configs)
        addSignal(sig);
}

// ============================================================
//  数据更新（性能优化：批量 replot）
// ============================================================

void GraphicView::clearData()
{
    for (auto &sd : m_signals) {
        if (sd.graph)
            sd.graph->data()->clear();
        sd.rawData.clear();
        sd.sampleSeq = 0;
        sd.hasMinMax = false;
        sd.minMaxDirty = false;
        sd.dataMin = 0.0;
        sd.dataMax = 0.0;
        sd.cacheValid = false;
        sd.displayDirty = true;
    }
    m_dataDirty = true;
    m_currentTime = 0.0;
    refreshTimeAxis();
    m_plot->replot();
    updateStatusBar();
}

// ============================================================
//  环形缓冲写入 / min/max 维护 / 视口降采样重建
// ============================================================

void GraphicView::pushSample(SignalData &sd, double t, double v)
{
    // P1-1: store-backed signals must not twin SampleStore into rawData.
    if (sd.storeSubscribed)
        return;

    const int capLimit = m_rawMaxCapacity;
    if (sd.rawData.full() && sd.rawData.capacity() < capLimit) {
        const int next = qMin(capLimit, qMax(sd.rawData.capacity() * 2, 4096));
        sd.rawData.growTo(next);
    }
    if (sd.rawData.push({t, v})) {
        sd.minMaxDirty = true;
    }
    if (!sd.hasMinMax) {
        sd.dataMin = v;
        sd.dataMax = v;
        sd.hasMinMax = true;
    } else {
        if (v < sd.dataMin) sd.dataMin = v;
        if (v > sd.dataMax) sd.dataMax = v;
    }
    // P1-3: per-signal dirty only (do not force full-view rebuild via m_dataDirty).
    sd.displayDirty = true;
}

void GraphicView::ensureMinMax(SignalData &sd)
{
    // P0-1: prefer SampleStore running range (maintained on worker ingest)
    if (sd.storeSubscribed) {
        double mn = 0.0, mx = 0.0;
        if (SampleStore::instance()->valueRange(sd.storeKey, &mn, &mx)) {
            sd.dataMin = mn;
            sd.dataMax = mx;
            sd.hasMinMax = true;
            sd.minMaxDirty = false;
            return;
        }
    }
    if (!sd.minMaxDirty)
        return;
    const int n = sd.rawData.size();
    if (n == 0) {
        sd.hasMinMax = false;
        sd.minMaxDirty = false;
        return;
    }
    double mn = sd.rawData.at(0).v;
    double mx = mn;
    for (int i = 1; i < n; ++i) {
        const double v = sd.rawData.at(i).v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    sd.dataMin = mn;
    sd.dataMax = mx;
    sd.hasMinMax = true;
    sd.minMaxDirty = false;
}

void GraphicView::refreshDisplayData()
{
    if (m_signals.isEmpty())
        return;

    QCPAxis *primaryX = primaryXAxis();
    if (!primaryX)
        return;
    const QCPRange vp = primaryX->range();

    // Target points ≈ 2 × viewport pixel width (line budget).
    // With markers on, raise budget so local zoom shows real samples (CANoe-like).
    int targetPoints = std::max(400, m_plot->width() * graphic::POINTS_PER_PIXEL);
    if (m_showPoints)
        targetPoints = std::max(targetPoints, std::min(20000, m_plot->width() * 8));
    SampleStore *store = SampleStore::instance();

    for (auto &sd : m_signals) {
        if (!sd.graph)
            continue;
        // P1-3: skip hidden signals (no LOD copy / graph rebuild).
        if (sd.userHidden) {
            if (sd.graph->dataCount() > 0)
                sd.graph->data()->clear();
            sd.cacheValid = false;
            sd.displayDirty = false;
            continue;
        }
        // Per-signal dirty + viewport cache; m_dataDirty is global clear/invalidate only.
        if (!m_dataDirty && !sd.displayDirty && sd.cacheValid &&
            sd.cachedT1 == vp.lower && sd.cachedT2 == vp.upper &&
            sd.cachedShowPoints == m_showPoints)
            continue;

        QVector<QCPGraphData> graphData;

        // Prefer store: raw when viewport sample count ≤ budget, else MinMax LOD.
        bool usedStore = false;
        if (sd.storeSubscribed) {
            QVector<SignalSample> disp;
            const int n = store->copyDownsampled(
                sd.storeKey, vp.lower, vp.upper, targetPoints, &disp);
            if (n > 0) {
                graphData.reserve(disp.size());
                for (const auto &s : disp)
                    graphData.append(QCPGraphData(s.t, s.v));
                usedStore = true;
            }
        }

        if (!usedStore) {
            const QVector<graphic::Sample> disp =
                graphic::downsample(sd.rawData, vp.lower, vp.upper, targetPoints, m_dsStrategy);
            graphData.reserve(disp.size());
            for (const auto &s : disp)
                graphData.append(QCPGraphData(s.t, s.v));
        }

        sd.graph->data()->set(graphData, true);
        sd.cachedT1 = vp.lower;
        sd.cachedT2 = vp.upper;
        sd.cachedShowPoints = m_showPoints;
        sd.cacheValid = true;
        sd.displayDirty = false;
    }
    m_dataDirty = false;
    updateCursorDecorations();
}

void GraphicView::onFrame(const CanFrame &frame)
{
    // Live path uses SampleStore; keep for rare single-frame callers / file tools
    QVector<CanFrame> one;
    one.append(frame);
    onFrames(one);
}

void GraphicView::onFrames(const QVector<CanFrame> &frames)
{
    // Phase B3: live measurement no longer decodes here — SampleStore::ingestFrames
    // runs once in the shell; this view pulls on m_samplePullTimer.
    // Keep a lightweight path for paused time tracking / legacy direct callers.
    if (frames.isEmpty())
        return;
    if (m_paused) {
        m_currentTime = frames.last().timestamp;
        return;
    }
    // If store has no subscription yet (e.g. mid-construction), fall back to local decode
    bool anySub = false;
    for (const auto &sd : m_signals) {
        if (sd.storeSubscribed) {
            anySub = true;
            break;
        }
    }
    if (anySub) {
        m_currentTime = frames.last().timestamp;
        return;
    }

    bool hasData = false;
    for (const auto &frame : frames) {
        m_currentTime = frame.timestamp;
        const quint64 key = (quint64(frame.id & 0x1FFFFFFF) << 1) |
                            (frame.extended ? 1ull : 0ull);
        const auto idxs = m_idIndex.value(key);
        for (int si : idxs) {
            auto &sd = m_signals[si];
            const double val = extractValue(frame, sd.config);
            if (std::isnan(val))
                continue;
            pushSample(sd, frame.timestamp, val);
            hasData = true;
        }
    }
    if (hasData) {
        const double tEnd = m_currentTime;
        const double tStart = std::max(0.0, tEnd - m_timeWindow);
        setXRangeAll(QCPRange(tStart, tEnd), false);
        m_replotPending = true;
        if (isVisible() && !m_replotTimer.isActive())
            m_replotTimer.start();
    }
}

void GraphicView::subscribeStore(SignalData &sd,
                                 const QVector<CanFrame> *history, int historyCount)
{
    sd.storeKey = SampleKey::fromSignal(sd.config.canId, sd.config.extended, sd.config.dbcSig);
    sd.sampleSeq = 0;
    sd.storeSubscribed = SampleStore::instance()->subscribe(sd.storeKey, sd.config.dbcSig);
    if (!sd.storeSubscribed)
        return;

    // P1-1: drop any local twin — SampleStore is SoT for live/subscribed series.
    sd.rawData.clear();
    sd.rawData.reserve(1);

    if (history && !history->isEmpty()) {
        const int count = historyCount < 0 ? history->size()
                                           : qMin(historyCount, history->size());
        // Cap backfill on GUI thread (full CaptureLog dump can freeze / OOM)
        constexpr int kMaxHistoryBackfill = 50000;
        const int start = qMax(0, count - kMaxHistoryBackfill);
        QVector<SignalSample> samples;
        samples.reserve(qMin(count - start, kMaxHistoryBackfill));
        for (int i = start; i < count; ++i) {
            const CanFrame &f = history->at(i);
            if ((f.id & 0x1FFFFFFF) != (sd.config.canId & 0x1FFFFFFF) ||
                f.extended != sd.config.extended)
                continue;
            const double val = extractValue(f, sd.config);
            if (std::isnan(val))
                continue;
            samples.append({f.timestamp, val});
        }
        if (!samples.isEmpty())
            SampleStore::instance()->appendSamples(sd.storeKey, samples);
    }
}

void GraphicView::unsubscribeStore(SignalData &sd)
{
    if (!sd.storeSubscribed)
        return;
    SampleStore::instance()->unsubscribe(sd.storeKey);
    sd.storeSubscribed = false;
    sd.sampleSeq = 0;
}

void GraphicView::pullFromSampleStore()
{
    if (m_paused || m_signals.isEmpty())
        return;

    // T5: hidden Graphic — advance sample cursors to tip only (no twin push / replot).
    if (!isVisible()) {
        SampleStore *store = SampleStore::instance();
        for (auto &sd : m_signals) {
            if (!sd.storeSubscribed)
                continue;
            SignalSample latest;
            quint64 tipSeq = sd.sampleSeq;
            if (store->latestSample(sd.storeKey, &latest, &tipSeq)) {
                sd.sampleSeq = tipSeq;
                m_currentTime = latest.t;
            }
        }
        return;
    }

    bool hasData = false;
    SampleStore *store = SampleStore::instance();

    for (auto &sd : m_signals) {
        if (!sd.storeSubscribed)
            continue;

        SignalSample latest;
        quint64 tipSeq = sd.sampleSeq;
        if (!store->latestSample(sd.storeKey, &latest, &tipSeq))
            continue;
        if (tipSeq == sd.sampleSeq)
            continue;

        // P1-1: no local raw twin — only advance cursor; display uses LOD.
        // P1-3: mark only this signal dirty (viewport scroll still invalidates via cache miss).
        sd.sampleSeq = tipSeq;
        m_currentTime = latest.t;
        sd.displayDirty = true;

        double mn = 0.0, mx = 0.0;
        if (store->valueRange(sd.storeKey, &mn, &mx)) {
            sd.dataMin = mn;
            sd.dataMax = mx;
            sd.hasMinMax = true;
            sd.minMaxDirty = false;
            if (QCPAxis *ya = valueAxisFor(sd)) {
                const double curMin = ya->range().lower;
                const double curMax = ya->range().upper;
                bool changed = false;
                double lo = curMin, hi = curMax;
                if (mn < curMin) {
                    lo = mn;
                    changed = true;
                }
                if (mx > curMax) {
                    hi = mx;
                    changed = true;
                }
                if (changed) {
                    const double pad = (hi - lo) * 0.05;
                    ya->setRange(lo - pad, hi + pad);
                }
            }
        }

        hasData = true;
    }

    if (hasData) {
        const double tEnd = m_currentTime;
        const double tStart = std::max(0.0, tEnd - m_timeWindow);
        setXRangeAll(QCPRange(tStart, tEnd), false);
        m_replotPending = true;
        if (isVisible() && !m_replotTimer.isActive())
            m_replotTimer.start();
    }
}

void GraphicView::onSamplePullTimer()
{
    updateSamplePullBudget();
    pullFromSampleStore();
}

void GraphicView::updateSamplePullBudget()
{
    // Visible: match replot interval. Hidden views stop the timer in hideEvent (B6).
    if (!isVisible())
        return;
    if (m_samplePullTimer.interval() != m_replotIntervalMs)
        m_samplePullTimer.setInterval(m_replotIntervalMs);
}

void GraphicView::rebuildIdIndex()
{
    m_idIndex.clear();
    for (int i = 0; i < m_signals.size(); ++i) {
        const auto &c = m_signals[i].config;
        const quint64 key = (quint64(c.canId & 0x1FFFFFFF) << 1) |
                            (c.extended ? 1ull : 0ull);
        m_idIndex[key].append(i);
    }
}

void GraphicView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // B6: resume composite timers only while visible (match Watcher/Trace T5).
    if (!m_samplePullTimer.isActive())
        m_samplePullTimer.start();
    if (!m_valueTimer.isActive())
        m_valueTimer.start();
    updateSamplePullBudget();
    pullFromSampleStore();
    if (m_replotPending && !m_replotTimer.isActive())
        m_replotTimer.start();
}

void GraphicView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    // B6: never composite in background — tip cursors once, then stop timers.
    SampleStore *store = SampleStore::instance();
    for (auto &sd : m_signals) {
        if (!sd.storeSubscribed)
            continue;
        SignalSample latest;
        quint64 tipSeq = sd.sampleSeq;
        if (store->latestSample(sd.storeKey, &latest, &tipSeq)) {
            sd.sampleSeq = tipSeq;
            m_currentTime = latest.t;
        }
    }
    m_samplePullTimer.stop();
    m_valueTimer.stop();
    m_replotTimer.stop();
}

void GraphicView::onReplotTimeout()
{
    if (!m_replotPending)
        return;
    if (!isVisible())
        return;
    m_replotPending = false;
    // refreshDisplayData rebuilds only dirty / viewport-missed graphs; always
    // replot for live X-axis scroll + cursors (cheap when graph data unchanged).
    refreshDisplayData();
    m_plot->replot(QCustomPlot::rpQueuedReplot);

    if (m_cursorMode != CursorMode::None)
        updateCursorValues();
    updateStatusBar();
}

// ============================================================
//  拖放加载文件
// ============================================================

void GraphicView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        for (const auto &url : urls) {
            QString suffix = QFileInfo(url.toLocalFile()).suffix().toLower();
            if (CanFileIO::formatFromSuffix(suffix) != CanFileIO::Format::Unknown) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

void GraphicView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void GraphicView::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    QString path = event->mimeData()->urls().first().toLocalFile();
    QString suffix = QFileInfo(path).suffix().toLower();
    if (CanFileIO::formatFromSuffix(suffix) == CanFileIO::Format::Unknown) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    loadFile(path);
}

void GraphicView::loadFile(const QString &path)
{
    clearData();

    auto *thread = QThread::create([this, path]() {
        QVector<CanFrame> frames;
        auto reader = CanFileIOFactory::createReader(path);
        if (!reader || !reader->open(path)) {
            QMetaObject::invokeMethod(this, [this]() {
                emit fileLoaded(-1);
            }, Qt::QueuedConnection);
            return;
        }
        int count = reader->readAll(frames);
        reader->close();

        QMetaObject::invokeMethod(this, [this, frames, count]() {
            if (count > 0) {
                double tFirst = -1.0;
                for (const auto &frame : frames) {
                    m_currentTime = frame.timestamp;
                    if (tFirst < 0)
                        tFirst = frame.timestamp;
                    for (auto &sd : m_signals) {
                        if ((frame.id & 0x1FFFFFFF) == sd.config.canId &&
                            frame.extended == sd.config.extended) {
                            double val = extractValue(frame, sd.config);
                            if (!std::isnan(val))
                                pushSample(sd, frame.timestamp, val);
                        }
                    }
                }
                // X 轴适配全部数据范围（而非时间窗口截断）
                double tStart = (tFirst > 0.0 ? tFirst : 0.0);
                double tEnd = m_currentTime;
                if (tEnd <= tStart)
                    tEnd = tStart + 1.0;
                setXRangeAll(QCPRange(tStart, tEnd), false);
                // Y 轴适配（当前生效轴：分栏/叠加，基于原始数据 min/max）
                for (auto &sd : m_signals) {
                    ensureMinMax(sd);
                    if (QCPAxis *ya = valueAxisFor(sd); ya && sd.hasMinMax) {
                        double margin = (sd.dataMax - sd.dataMin) * 0.05;
                        if (margin <= 0) margin = 1.0;
                        ya->setRange(sd.dataMin - margin, sd.dataMax + margin);
                    }
                }
                updateCursorDecorations();
                refreshDisplayData();
                m_plot->replot();
            }
            emit fileLoaded(count);
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

double GraphicView::extractValue(const CanFrame &frame, const Signal &sig) const
{
    return sig.dbcSig.decode(frame.data);
}

quint64 GraphicView::extractRaw(const CanFrame &frame, const Signal &sig) const
{
    return sig.dbcSig.rawDecode(frame.data);
}

void GraphicView::refreshTimeAxis()
{
    double tEnd = m_currentTime;
    double tStart = std::max(0.0, tEnd - m_timeWindow);
    // Avoid degenerate [0,0] after clearData — empty range makes downsample return nothing
    if (tEnd <= tStart)
        tEnd = tStart + m_timeWindow;
    setXRangeAll(QCPRange(tStart, tEnd), false);
    // blocker blocked rangeChanged → rebuild display data manually
    refreshDisplayData();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

// ============================================================
//  信号列表
// ============================================================

void GraphicView::updateSignalList()
{
    m_signalTree->blockSignals(true);
    m_signalTree->clear();

    for (int i = 0; i < m_signals.size(); ++i) {
        const auto &sd = m_signals[i];
        auto *item = new QTreeWidgetItem();
        // 列 0: 色块图标（CANoe 信号栏风格）
        QPixmap colorPix(12, 12);
        colorPix.fill(sd.config.color);
        item->setIcon(0, QIcon(colorPix));
        // 列 1: 信号名 + 显隐复选框
        item->setText(1, sd.config.name);
        item->setCheckState(1, sd.userHidden ? Qt::Unchecked : Qt::Checked);
        item->setForeground(1, sd.config.color);
        // 列 2: 物理值（卡尺或实时刷新）
        item->setText(2, "—");
        // 列 3: 原始值
        item->setText(3, "—");
        // 列 4: 单位
        item->setText(4, sd.config.dbcSig.unit);
        // 列 5: Min
        item->setText(5, sd.hasMinMax ? QString::number(sd.dataMin, 'f', 2) : "—");
        // 列 6: Max
        item->setText(6, sd.hasMinMax ? QString::number(sd.dataMax, 'f', 2) : "—");
        // 列 7: ID
        item->setText(7, QString("0x%1").arg(sd.config.canId, 0, 16).toUpper());
        // 列 8: 点数
        item->setText(8, QString::number(
            sd.storeSubscribed
                ? SampleStore::instance()->sampleCount(sd.storeKey)
                : sd.rawData.size()));
        item->setData(0, Qt::UserRole, i);
        m_signalTree->addTopLevelItem(item);
    }
    m_signalTree->blockSignals(false);
    updateStatusBar();
}

void GraphicView::invertSignalSelection()
{
    for (int i = 0; i < m_signalTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_signalTree->topLevelItem(i);
        it->setSelected(!it->isSelected());
    }
}

void GraphicView::updateSignalValues()
{
    // Cursor mode: list values follow cursors (CANoe-style).
    if (m_cursorMode != CursorMode::None)
        return;

    SampleStore *store = SampleStore::instance();
    for (int i = 0; i < m_signals.size() && i < m_signalTree->topLevelItemCount(); ++i) {
        auto *item = m_signalTree->topLevelItem(i);
        auto &sd = m_signals[i];

        double physVal = 0.0;
        bool got = false;
        if (sd.storeSubscribed) {
            SignalSample latest;
            if (store->latestSample(sd.storeKey, &latest)) {
                physVal = latest.v;
                got = true;
            }
        } else if (!sd.rawData.empty()) {
            physVal = sd.rawData.at(sd.rawData.size() - 1).v;
            got = true;
        }

        if (!got) {
            item->setText(2, QStringLiteral("—"));
            item->setText(3, QStringLiteral("—"));
            item->setText(8, QStringLiteral("0"));
            continue;
        }

        double factor = sd.config.dbcSig.factor;
        if (factor == 0) factor = 1.0;
        quint64 rawVal = static_cast<quint64>((physVal - sd.config.dbcSig.offset) / factor + 0.5);

        item->setText(2, QString::number(physVal, 'f', 3));
        item->setText(3, QString::number(rawVal));
        ensureMinMax(sd);
        item->setText(5, sd.hasMinMax ? QString::number(sd.dataMin, 'f', 2) : QStringLiteral("—"));
        item->setText(6, sd.hasMinMax ? QString::number(sd.dataMax, 'f', 2) : QStringLiteral("—"));
        item->setText(8, QString::number(
            sd.storeSubscribed ? store->sampleCount(sd.storeKey) : sd.rawData.size()));
    }
}

void GraphicView::updateCursorValues()
{
    if (m_cursorMode == CursorMode::None) {
        for (int i = 0; i < m_signalTree->topLevelItemCount(); ++i) {
            auto *item = m_signalTree->topLevelItem(i);
            item->setText(2, "—");
            item->setText(3, "—");
        }
        return;
    }

    for (int i = 0; i < m_signals.size() && i < m_signalTree->topLevelItemCount(); ++i) {
        auto *item = m_signalTree->topLevelItem(i);
        auto &sd = m_signals[i];

        double physVal = 0.0;
        bool got = false;
        if (sd.storeSubscribed)
            got = SampleStore::instance()->valueAtTime(sd.storeKey, m_cursor1Time, &physVal);
        if (!got && !sd.rawData.empty())
            got = valueAtTime(sd.rawData, m_cursor1Time, physVal);
        if (got) {
            double factor = sd.config.dbcSig.factor;
            if (factor == 0) factor = 1.0;
            quint64 rawVal = static_cast<quint64>((physVal - sd.config.dbcSig.offset) / factor + 0.5);
            item->setText(2, QString::number(physVal, 'f', 3));
            item->setText(3, QString::number(rawVal));
        } else {
            item->setText(2, "—");
            item->setText(3, "—");
        }

        if (m_cursorMode == CursorMode::Double && m_cursor2) {
            double physVal2 = 0.0;
            bool got2 = false;
            if (sd.storeSubscribed)
                got2 = SampleStore::instance()->valueAtTime(sd.storeKey, m_cursor2Time, &physVal2);
            if (!got2 && !sd.rawData.empty())
                got2 = valueAtTime(sd.rawData, m_cursor2Time, physVal2);
            if (got2) {
                double delta = physVal2 - physVal;
                item->setText(2, QString("%1 → %2 (Δ%3)")
                    .arg(physVal, 0, 'f', 3)
                    .arg(physVal2, 0, 'f', 3)
                    .arg(delta, 0, 'f', 3));
            }
        }
    }

    if (m_cursorMode == CursorMode::None || !m_cursorInfoLabel) {
        if (m_cursorInfoLabel) m_cursorInfoLabel->setVisible(false);
        return;
    }

    m_cursorInfoLabel->setVisible(true);

    if (m_cursorMode == CursorMode::Single) {
        m_cursorInfoLabel->setText(
            QString("T₁ = %1s").arg(m_cursor1Time, 0, 'f', 4));
    } else if (m_cursorMode == CursorMode::Double) {
        double deltaT = m_cursor2Time - m_cursor1Time;
        double freq = (std::abs(deltaT) > 1e-9) ? 1.0 / std::abs(deltaT) : 0.0;

        QString info = QString("T₁ = %1s  T₂ = %2s  ΔT = %3s")
            .arg(m_cursor1Time, 0, 'f', 4)
            .arg(m_cursor2Time, 0, 'f', 4)
            .arg(deltaT, 0, 'f', 4);

        if (freq > 0)
            info += QString("  f ≈ %1 Hz").arg(freq, 0, 'f', 2);

        // Ruler metrics only — per-signal Δ lives in the signal list columns.
        m_cursorInfoLabel->setText(info);
    }
}

// ============================================================
//  卡尺系统
// ============================================================

void GraphicView::ensureCursors()
{
    // Time labels use ptAbsolute; updateCursorDecorations keeps them in sync
    auto setupDecor = [this](QCPItemText *t, const QColor &c) {
        t->position->setType(QCPItemPosition::ptAbsolute);
        t->setPositionAlignment(Qt::AlignHCenter | Qt::AlignTop);
        t->setColor(c);
        t->setBrush(QBrush(m_palette.nameTagBg));
        t->setPen(QPen(m_palette.nameTagBorder, 1));
        t->setPadding(QMargins(2, 0, 2, 0));
        QFont f("Consolas", 8);
        t->setFont(f);
    };
    auto setupAbsLine = [](QCPItemStraightLine *line, const QPen &pen) {
        // Pixel coords + no axis-rect clip: otherwise the line is interpreted as
        // plot units (off-screen) or clipped to a single track.
        line->setClipToAxisRect(false);
        line->setLayer(QStringLiteral("overlay"));
        line->point1->setType(QCPItemPosition::ptAbsolute);
        line->point2->setType(QCPItemPosition::ptAbsolute);
        line->setPen(pen);
        line->setVisible(false);
    };
    if (!m_cursor1) {
        m_cursor1 = new QCPItemStraightLine(m_plot);
        setupAbsLine(m_cursor1, QPen(m_palette.cursor1, 1.5, Qt::DashLine));
        m_cursor1Handle = new CursorHandleItem(m_plot, m_palette.cursor1);
        m_cursor1Handle->setLayer(QStringLiteral("overlay"));
        m_cursor1Label = new QCPItemText(m_plot);
        m_cursor1Label->setLayer(QStringLiteral("overlay"));
        setupDecor(m_cursor1Label, m_palette.cursor1);
    }
    if (!m_cursor2) {
        m_cursor2 = new QCPItemStraightLine(m_plot);
        setupAbsLine(m_cursor2, QPen(m_palette.cursor2, 1.5, Qt::DashLine));
        m_cursor2Handle = new CursorHandleItem(m_plot, m_palette.cursor2);
        m_cursor2Handle->setLayer(QStringLiteral("overlay"));
        m_cursor2Label = new QCPItemText(m_plot);
        m_cursor2Label->setLayer(QStringLiteral("overlay"));
        setupDecor(m_cursor2Label, m_palette.cursor2);
    }
}

void GraphicView::ensureCurrentTimeLine()
{
    if (!m_currentTimeLine) {
        m_currentTimeLine = new QCPItemStraightLine(m_plot);
        m_currentTimeLine->setClipToAxisRect(false);
        m_currentTimeLine->setLayer(QStringLiteral("overlay"));
        m_currentTimeLine->point1->setType(QCPItemPosition::ptAbsolute);
        m_currentTimeLine->point2->setType(QCPItemPosition::ptAbsolute);
        m_currentTimeLine->setPen(QPen(m_palette.timeLine, 1.5));
        m_currentTimeLine->setVisible(false);
    }
}

void GraphicView::ensureTrackLine()
{
    if (!m_trackLine) {
        m_trackLine = new QCPItemStraightLine(m_plot);
        m_trackLine->setClipToAxisRect(false);
        m_trackLine->setLayer(QStringLiteral("overlay"));
        m_trackLine->point1->setType(QCPItemPosition::ptAbsolute);
        m_trackLine->point2->setType(QCPItemPosition::ptAbsolute);
        m_trackLine->setPen(QPen(m_palette.trackCursor, 1, Qt::DotLine));
        m_trackLine->setVisible(false);
    }
    if (!m_trackLabel) {
        m_trackLabel = new QCPItemText(m_plot);
        m_trackLabel->position->setType(QCPItemPosition::ptAbsolute);
        m_trackLabel->setPositionAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        m_trackLabel->setColor(m_palette.axisText);
        m_trackLabel->setBrush(QBrush(m_palette.nameTagBg));
        m_trackLabel->setPen(QPen(m_palette.nameTagBorder, 1));
        m_trackLabel->setPadding(QMargins(2, 0, 2, 0));
        QFont f("Consolas", 8);
        m_trackLabel->setFont(f);
        m_trackLabel->setVisible(false);
    }
}

QRect GraphicView::cursorSpanRect() const
{
    QRect rc;
    for (const auto &sd : m_signals) {
        if (sd.axisRect && sd.axisRect->visible())
            rc = rc.isNull() ? sd.axisRect->rect() : rc.united(sd.axisRect->rect());
    }
    if (m_overlayRect && m_overlayRect->visible())
        rc = rc.isNull() ? m_overlayRect->rect() : rc.united(m_overlayRect->rect());
    if (rc.isNull()) {
        if (QCPAxisRect *pr = primaryRect())
            rc = pr->rect();
    }
    if (rc.isNull() && m_plot && m_plot->axisRectCount() > 0)
        rc = m_plot->axisRect(0)->rect();
    return rc;
}

void GraphicView::updateCursorDecorations()
{
    QCPAxis *xAxis = primaryXAxis();
    const QRect rc = cursorSpanRect();
    const bool valid = xAxis && !rc.isNull();

    // Vertical dashed line spans all visible tracks (CANoe Graphic rulers)
    auto placeLine = [&](QCPItemStraightLine *line, double t, bool placed) {
        if (!line)
            return;
        if (!valid || !placed) {
            line->setVisible(false);
            return;
        }
        const double px = xAxis->coordToPixel(t);
        if (px < rc.left() - 60 || px > rc.right() + 60) {
            line->setVisible(false);
            return;
        }
        line->point1->setType(QCPItemPosition::ptAbsolute);
        line->point2->setType(QCPItemPosition::ptAbsolute);
        line->setClipToAxisRect(false);
        line->point1->setCoords(px, rc.top());
        line->point2->setCoords(px, rc.bottom());
        line->setVisible(true);
    };
    auto placeDecor = [&](CursorHandleItem *handle, QCPItemText *label,
                          double t, const QString &text, bool placed) {
        if (!handle || !label)
            return;
        if (!valid || !placed) {
            handle->setVisible(false);
            label->setVisible(false);
            return;
        }
        const double px = xAxis->coordToPixel(t);
        const bool inView = px >= rc.left() - 40 && px <= rc.right() + 40;
        handle->setVisible(inView);
        label->setVisible(inView);
        if (!inView)
            return;
        handle->position->setCoords(QPointF(px, rc.top() + 1));
        label->position->setCoords(QPointF(px, rc.top() + 15));
        label->setText(text);
    };

    placeLine(m_cursor1, m_cursor1Time, m_cursor1Placed);
    placeLine(m_cursor2, m_cursor2Time,
              m_cursor2Placed && m_cursorMode == CursorMode::Double);
    // Current-time indicator: keep full-height span when already shown
    if (m_currentTimeLine) {
        const bool showNow = m_currentTimeLine->visible() || m_currentTime > 0;
        placeLine(m_currentTimeLine, m_currentTime, showNow && m_currentTime > 0);
    }

    placeDecor(m_cursor1Handle, m_cursor1Label, m_cursor1Time,
               QString::number(m_cursor1Time, 'f', 3) + "s", m_cursor1Placed);
    if (m_cursorMode == CursorMode::Double)
        placeDecor(m_cursor2Handle, m_cursor2Label, m_cursor2Time,
                   QString::number(m_cursor2Time, 'f', 3) + "s", m_cursor2Placed);
    else {
        if (m_cursor2Handle) m_cursor2Handle->setVisible(false);
        if (m_cursor2Label) m_cursor2Label->setVisible(false);
    }
}

void GraphicView::setCursorMode(CursorMode mode)
{
    m_cursorMode = mode;

    if (mode == CursorMode::None) {
        if (m_cursor1) { m_plot->removeItem(m_cursor1); m_cursor1 = nullptr; }
        if (m_cursor2) { m_plot->removeItem(m_cursor2); m_cursor2 = nullptr; }
        if (m_cursor1Handle) { m_plot->removeItem(m_cursor1Handle); m_cursor1Handle = nullptr; }
        if (m_cursor2Handle) { m_plot->removeItem(m_cursor2Handle); m_cursor2Handle = nullptr; }
        if (m_cursor1Label) { m_plot->removeItem(m_cursor1Label); m_cursor1Label = nullptr; }
        if (m_cursor2Label) { m_plot->removeItem(m_cursor2Label); m_cursor2Label = nullptr; }
        m_draggingCursor = 0;
        m_cursor1Placed = false;
        m_cursor2Placed = false;
        if (!m_panMode)
            m_plot->setCursor(Qt::ArrowCursor);
        updateCursorValues();
        m_plot->replot();
        return;
    }

    // Enter placement mode: wait for plot click (CANoe Graphic rulers)
    ensureCursors();
    m_cursor1Placed = false;
    m_cursor2Placed = false;
    if (m_cursor1) m_cursor1->setVisible(false);
    if (m_cursor2) m_cursor2->setVisible(false);
    if (m_cursor1Handle) m_cursor1Handle->setVisible(false);
    if (m_cursor2Handle) m_cursor2Handle->setVisible(false);
    if (m_cursor1Label) m_cursor1Label->setVisible(false);
    if (m_cursor2Label) m_cursor2Label->setVisible(false);
    if (!m_panMode)
        m_plot->setCursor(Qt::CrossCursor);

    updateCursorValues();
    m_plot->replot();
}

void GraphicView::placeOrMoveCursorAt(const QPoint &pos)
{
    QCPAxis *xAxis = primaryXAxis();
    if (!xAxis || m_cursorMode == CursorMode::None)
        return;

    ensureCursors();
    const double t = xAxis->pixelToCoord(pos.x());

    if (m_cursorMode == CursorMode::Single) {
        m_cursor1Placed = true;
        moveCursor(1, t);
        return;
    }

    // Double: place C1, then C2; afterwards move the nearer one
    if (!m_cursor1Placed) {
        m_cursor1Placed = true;
        moveCursor(1, t);
        return;
    }
    if (!m_cursor2Placed) {
        m_cursor2Placed = true;
        moveCursor(2, t);
        return;
    }
    if (std::abs(t - m_cursor2Time) < std::abs(t - m_cursor1Time))
        moveCursor(2, t);
    else
        moveCursor(1, t);
}

void GraphicView::moveCursor(int which, double time)
{
    if (which == 1 && m_cursor1)
        m_cursor1Time = time;
    else if (which == 2 && m_cursor2)
        m_cursor2Time = time;
    // 像素位置（手柄/标签/线）由 updateCursorDecorations 统一维护
    updateCursorDecorations();
    updateCursorValues();
    m_plot->replot(QCustomPlot::rpQueuedReplot);

    // 多视图游标联动（同步中不发射，防回环）
    if (!m_syncingCursor && m_cursorLink)
        emit cursorMoved(which, time);
}

void GraphicView::onSyncCursor(int which, double time)
{
    if (m_syncingCursor || !m_cursorLink)
        return;
    if (m_cursorMode == CursorMode::None)
        return;
    // 本视图无双卡尺时，卡尺 2 降级为卡尺 1
    if (which == 2 && m_cursorMode != CursorMode::Double)
        which = 1;

    m_syncingCursor = true;
    moveCursor(which, time);
    m_syncingCursor = false;
}

bool GraphicView::valueAtTime(const RingBuffer<graphic::Sample> &raw, double time, double &outVal)
{
    const int n = raw.size();
    if (n == 0)
        return false;

    // 二分定位第一个 t >= time
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (raw.at(mid).t < time)
            lo = mid + 1;
        else
            hi = mid;
    }

    if (lo == n) {
        // 超出最后一点 → 取最后值（阶梯保持语义）
        outVal = raw.at(n - 1).v;
        return true;
    }
    if (lo == 0) {
        // 早于第一点 → 取第一值
        outVal = raw.at(0).v;
        return true;
    }

    // [lo-1, lo] 之间线性插值
    const graphic::Sample &p0 = raw.at(lo - 1);
    const graphic::Sample &p1 = raw.at(lo);
    if (p1.t == p0.t) {
        outVal = p1.v;
    } else {
        double ratio = (time - p0.t) / (p1.t - p0.t);
        outVal = p0.v + ratio * (p1.v - p0.v);
    }
    return true;
}

// ============================================================
//  适应窗口 & 导出
// ============================================================

void GraphicView::fitAll()
{
    pushZoomState();
    bool hasData = false;
    double tMin = 0.0, tMax = 0.0;
    SampleStore *store = SampleStore::instance();
    for (const auto &sd : m_signals) {
        double t0 = 0.0, t1 = 0.0;
        bool ok = false;
        if (sd.storeSubscribed)
            ok = store->timeRange(sd.storeKey, &t0, &t1);
        else if (!sd.rawData.empty()) {
            t0 = sd.rawData.at(0).t;
            t1 = sd.rawData.at(sd.rawData.size() - 1).t;
            ok = true;
        }
        if (!ok)
            continue;
        if (!hasData) {
            tMin = t0;
            tMax = t1;
            hasData = true;
        } else {
            tMin = std::min(tMin, t0);
            tMax = std::max(tMax, t1);
        }
    }
    if (hasData) {
        double margin = (tMax - tMin) * 0.02;
        if (margin <= 0)
            margin = 0.5;
        setXRangeAll(QCPRange(tMin - margin, tMax + margin), false);
        m_currentTime = std::max(m_currentTime, tMax);
    } else {
        double tEnd = m_currentTime;
        double tStart = std::max(0.0, tEnd - m_timeWindow);
        if (tEnd <= tStart)
            tEnd = tStart + m_timeWindow;
        setXRangeAll(QCPRange(tStart, tEnd), false);
    }
    for (auto &sd : m_signals) {
        ensureMinMax(sd);
        if (QCPAxis *ya = valueAxisFor(sd); ya && sd.hasMinMax) {
            double margin = (sd.dataMax - sd.dataMin) * 0.05;
            if (margin <= 0) margin = 1.0;
            ya->setRange(sd.dataMin - margin, sd.dataMax + margin);
        }
    }
    updateCursorDecorations();
    refreshDisplayData();
    m_plot->replot();
}

void GraphicView::fitXOnly()
{
    bool hasData = false;
    double tMin = 0.0, tMax = 0.0;
    SampleStore *store = SampleStore::instance();
    for (const auto &sd : m_signals) {
        double t0 = 0.0, t1 = 0.0;
        bool ok = false;
        if (sd.storeSubscribed)
            ok = store->timeRange(sd.storeKey, &t0, &t1);
        else if (!sd.rawData.empty()) {
            t0 = sd.rawData.at(0).t;
            t1 = sd.rawData.at(sd.rawData.size() - 1).t;
            ok = true;
        }
        if (!ok)
            continue;
        if (!hasData) {
            tMin = t0;
            tMax = t1;
            hasData = true;
        } else {
            tMin = std::min(tMin, t0);
            tMax = std::max(tMax, t1);
        }
    }
    if (!hasData)
        return;
    double margin = (tMax - tMin) * 0.02;
    if (margin <= 0)
        margin = 0.5;
    pushZoomState();
    setXRangeAll(QCPRange(tMin - margin, tMax + margin), false);
    updateCursorDecorations();
    refreshDisplayData();
    m_plot->replot();
}

void GraphicView::fitYOnly()
{
    pushZoomState();
    // 与 fitAll 的 Y 逻辑一致：基于原始数据 min/max + 5% 边距
    for (auto &sd : m_signals) {
        ensureMinMax(sd);
        if (QCPAxis *ya = valueAxisFor(sd); ya && sd.hasMinMax) {
            double margin = (sd.dataMax - sd.dataMin) * 0.05;
            if (margin <= 0)
                margin = 1.0;
            ya->setRange(sd.dataMin - margin, sd.dataMax + margin);
        }
    }
    updateCursorDecorations();
    refreshDisplayData();
    m_plot->replot();
}

void GraphicView::setDbcManager(DbcManager *mgr)
{
    m_dbcManager = mgr;
}

void GraphicView::exportPlot()
{
    QString path = QFileDialog::getSaveFileName(
        this, "导出图表", "graphic.png", "PNG 图片 (*.png);;所有文件 (*.*)");
    if (path.isEmpty()) return;
    m_plot->savePng(path, 0, 0, 2.0, -1);
}

// ============================================================
//  时间格式化 & 状态栏
// ============================================================

QString GraphicView::formatTime(double seconds)
{
    if (seconds < 0) return "0.000s";
    int totalMs = static_cast<int>(seconds * 1000 + 0.5);
    int ms = totalMs % 1000;
    int totalSec = totalMs / 1000;
    int sec = totalSec % 60;
    int min = totalSec / 60;
    if (min > 0)
        return QString("%1:%2.%3s")
            .arg(min).arg(sec, 2, 10, QChar('0')).arg(ms, 3, 10, QChar('0'));
    return QString("%1.%2s").arg(sec).arg(ms, 3, 10, QChar('0'));
}

void GraphicView::updateStatusBar()
{
    QStringList parts;
    parts << QStringLiteral("Signals: %1").arg(m_signals.size());

    int totalPoints = 0;
    SampleStore *store = SampleStore::instance();
    for (const auto &sd : m_signals) {
        if (sd.storeSubscribed)
            totalPoints += store->sampleCount(sd.storeKey);
        else
            totalPoints += sd.rawData.size();
    }
    parts << QStringLiteral("Samples: %1").arg(totalPoints);
    parts << QStringLiteral("Time: %1").arg(formatTime(m_currentTime));
    parts << QStringLiteral("Window: %1s").arg(m_timeWindow);

    if (m_paused)
        parts << QStringLiteral("[Paused]");

    const QString text = parts.join(QStringLiteral("  |  "));
    if (m_statusLabel)
        m_statusLabel->setText(text);
    emit statusInfoChanged(text);
}

// ============================================================
// G15-P4: 信号列表列标题常量（索引与 updateSignalList()一致）
// ============================================================
static const QVector<QString> &getColumnHeaders()
{
    static const QVector<QString> headers = {
        QString(),                 // 0: 色块
        QStringLiteral("信号"),     // 1: 信号名（核心列）
        QStringLiteral("物理值"),   // 2
        QStringLiteral("原始值"),   // 3
        QStringLiteral("单位"),     // 4
        QStringLiteral("Min"),      // 5
        QStringLiteral("Max"),      // 6
        QStringLiteral("ID"),       // 7
        QStringLiteral("点数")        // 8
    };
    return headers;
}

// ============================================================
// G15 P3/P4: 信号列表列配置管理
// ============================================================

void GraphicView::showColumnVisibilityMenu(const QPoint &pos)
{
    QMenu menu(m_signalTree);
    menu.setWindowTitle("列显示设置");

    // 列 0：色块（固定，不可隐藏）
    auto *colorColAction = new QAction("\u25a1 色块", &menu);
    colorColAction->setCheckable(true);
    colorColAction->setChecked(true);
    colorColAction->setEnabled(false);
    menu.addAction(colorColAction);

    // 列 1：信号名（核心列，建议保留）
    for (int c = 1; c < 9; ++c) {
        QString name = getColumnHeaders()[c];
        if (name.isEmpty()) continue;

        auto *action = menu.addAction(name);
        action->setCheckable(true);

        // 检查当前可见性
        bool visible = !m_signalTree->isColumnHidden(c);
        action->setChecked(visible);

        connect(action, &QAction::toggled, this, [this, c, visible](bool on) {
            m_signalTree->setColumnHidden(c, !on);
            if (!on) {
                // 隐藏时保存宽度
                m_columnWidths[c] = m_signalTree->header()->sectionSize(c);
            } else {
                // 恢复时应用保存的宽度或默认值
                int width = m_columnWidths.value(c, 80 - c); // 简单降级逻辑
                if (width == 0) width = 60;
                m_signalTree->header()->resizeSection(c, width);
            }
            saveColumnConfig();
        });
    }

    menu.addSeparator();

    // “复位列宽”按钮
    auto *resetWidthAction = menu.addAction("\u21bb 复位列宽");
    connect(resetWidthAction, &QAction::triggered, this, [this]() {
        for (int c = 1; c < 9; ++c) {
            m_columnWidths.remove(c);
            if (c == 1) m_signalTree->header()->resizeSection(c, 120);
            else if (c == 2) m_signalTree->header()->resizeSection(c, 80);
            else if (c == 3) m_signalTree->header()->resizeSection(c, 60);
            else if (c <= 7) m_signalTree->header()->resizeSection(c, 60);
            else m_signalTree->header()->resizeSection(c, 50);
        }
        saveColumnConfig();
    });

    menu.exec(m_signalTree->mapToGlobal(pos));
}

void GraphicView::restoreColumnConfig()
{
    // TODO: 从 settings.json 加载（预留扩展接口）
    // 当前仅恢复默认的 Interactive 模式 + 初始宽度
    // 已在 setupUi() 中完成
}

void GraphicView::saveColumnConfig()
{
    // TODO: 保存到 settings.json（预留扩展接口）
    // 示例：QSettings().setValue("graphic.signalList.columns", m_columnVisibility);
}
