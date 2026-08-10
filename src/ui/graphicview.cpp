#include "graphicview.h"
#include "signalconfigdialog.h"

#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
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
#include <cmath>
#include <algorithm>
#include <functional>

#include "qcustomplot.h"
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"

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
    std::function<void(QWheelEvent*)> onWheel;
    std::function<void(QContextMenuEvent*)> onContextMenu;

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
        int totalMs = static_cast<int>(tick * 1000 + 0.5);
        int ms = totalMs % 1000;
        int totalSec = totalMs / 1000;
        int sec = totalSec % 60;
        int min = totalSec / 60;
        if (min > 0)
            return QString("%1:%2.%3")
                .arg(min).arg(sec, 2, 10, QChar('0')).arg(ms / 100);
        return QString("%1.%2").arg(sec).arg(ms / 100, 1, 10, QChar('0'));
    }
};

// ============================================================
//  GraphicView 实现
// ============================================================

GraphicView::GraphicView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setAcceptDrops(true);

    // 性能节流：50ms 定时批量重绘
    m_replotTimer.setInterval(REPLOT_INTERVAL_MS);
    m_replotTimer.setSingleShot(true);
    connect(&m_replotTimer, &QTimer::timeout, this, [this]() { onReplotTimeout(); });

    // 信号列表值刷新：200ms
    m_valueTimer.setInterval(VALUE_UPDATE_MS);
    connect(&m_valueTimer, &QTimer::timeout, this, [this]() { updateSignalValues(); });
    m_valueTimer.start();

    ensureCurrentTimeLine();
}

QColor GraphicView::autoColor(int index)
{
    static const QColor colors[] = {
        QColor(0x21, 0x96, 0xF3), // blue
        QColor(0xF4, 0x43, 0x36), // red
        QColor(0x4C, 0xAF, 0x50), // green
        QColor(0xFF, 0x98, 0x00), // orange
        QColor(0x9C, 0x27, 0xB0), // purple
        QColor(0x00, 0xBC, 0xD4), // cyan
        QColor(0xFF, 0xEB, 0x3B), // yellow
        QColor(0x79, 0x55, 0x48), // brown
    };
    return colors[index % 8];
}

void GraphicView::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ---- 工具栏 ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));
    m_toolbar->setStyleSheet(
        "QToolBar { background: #2d2d2d; border: none; border-bottom: 1px solid #3d3d3d; spacing: 2px; padding: 2px; }"
        "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 3px; "
        "padding: 3px 8px; color: #cccccc; font-size: 12px; min-width: 28px; }"
        "QToolButton:hover { background: #3d3d3d; border-color: #555; }"
        "QToolButton:checked { background: #0c5d8f; border-color: #1a8dcc; color: #fff; }"
        "QCheckBox { color: #cccccc; font-size: 12px; padding: 2px 6px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QComboBox { background: #3d3d3d; border: 1px solid #555; border-radius: 3px; "
        "padding: 2px 6px; color: #cccccc; font-size: 12px; min-width: 60px; }"
        "QComboBox:hover { border-color: #777; }"
        "QComboBox QAbstractItemView { background: #2d2d2d; border: 1px solid #555; "
        "selection-background-color: #0c5d8f; color: #cccccc; }"
    );

    auto makeBtn = [this](const QString &text, const QString &tip) -> QToolButton* {
        auto *btn = new QToolButton(m_toolbar);
        btn->setText(text);
        btn->setToolTip(tip);
        btn->setAutoRaise(true);
        return btn;
    };

    // 暂停/继续
    m_pauseBtn = makeBtn("⏸", "暂停/继续采集");
    m_pauseBtn->setCheckable(true);

    auto *zoomInBtn = makeBtn("＋", "放大");
    auto *zoomOutBtn = makeBtn("－", "缩小");
    auto *fitBtn = makeBtn("⤢", "适应窗口");
    auto *clearDataBtn = makeBtn("⟲", "清空数据");
    auto *exportBtn = makeBtn("📷", "导出为图片");

    // 时间窗口选择
    m_timeWindowCombo = new QComboBox(m_toolbar);
    m_timeWindowCombo->setToolTip("时间窗口");
    for (int sec : {1, 2, 5, 10, 30, 60, 120, 300, 600})
        m_timeWindowCombo->addItem(QString("%1s").arg(sec), sec);
    m_timeWindowCombo->setCurrentIndex(4); // 默认 30s

    m_pointsToggle = new QCheckBox("采样点", m_toolbar);
    m_pointsToggle->setToolTip("显示/隐藏采样点");
    m_pointsToggle->setChecked(m_showPoints);

    m_cursorSingleBtn = makeBtn("┊", "单卡尺");
    m_cursorSingleBtn->setCheckable(true);
    m_cursorDoubleBtn = makeBtn("┊┊", "双卡尺");
    m_cursorDoubleBtn->setCheckable(true);
    m_cursorClearBtn = makeBtn("✕", "清除卡尺");

    m_toolbar->addWidget(m_pauseBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(zoomInBtn);
    m_toolbar->addWidget(zoomOutBtn);
    m_toolbar->addWidget(fitBtn);
    m_toolbar->addWidget(clearDataBtn);
    m_toolbar->addSeparator();
    // 时间窗口标签 + 下拉框
    auto *twLabel = new QLabel("窗口:", m_toolbar);
    twLabel->setStyleSheet("color: #aaa; font-size: 12px; padding-left: 4px;");
    m_toolbar->addWidget(twLabel);
    m_toolbar->addWidget(m_timeWindowCombo);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_pointsToggle);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_cursorSingleBtn);
    m_toolbar->addWidget(m_cursorDoubleBtn);
    m_toolbar->addWidget(m_cursorClearBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(exportBtn);
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    mainLayout->addWidget(m_toolbar);

    // ---- 分割器: 信号列表 | 波形区 ----
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧: 信号列表 (QTreeWidget)
    auto *leftWidget = new QWidget(m_splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_signalTree = new QTreeWidget(leftWidget);
    m_signalTree->setColumnCount(8);
    m_signalTree->setHeaderLabels({"信号", "原始值", "物理值", "单位", "Min", "Max", "ID", "点数"});
    m_signalTree->setRootIsDecorated(false);
    m_signalTree->setAlternatingRowColors(true);
    m_signalTree->setMinimumWidth(320);
    m_signalTree->setStyleSheet(
        "QTreeWidget { background: #1e1e1e; color: #ccc; border: none; font-size: 12px; }"
        "QTreeWidget::item { padding: 2px 4px; }"
        "QTreeWidget::item:selected { background: #0c5d8f; color: #fff; }"
        "QHeaderView::section { background: #2d2d2d; color: #aaa; border: none; "
        "border-bottom: 1px solid #3d3d3d; padding: 3px 4px; font-size: 11px; }"
    );
    m_signalTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 8; ++c)
        m_signalTree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    leftLayout->addWidget(m_signalTree, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    btnBar->setSpacing(4);
    auto *addBtn = new QPushButton("+ 添加信号", leftWidget);
    addBtn->setStyleSheet("QPushButton { background: #2d5a2d; color: #ccc; border: 1px solid #3d7a3d; "
                          "border-radius: 3px; padding: 4px 8px; font-size: 12px; }"
                          "QPushButton:hover { background: #3d7a3d; }");
    auto *removeBtn = new QPushButton("- 删除信号", leftWidget);
    removeBtn->setStyleSheet("QPushButton { background: #5a2d2d; color: #ccc; border: 1px solid #7a3d3d; "
                              "border-radius: 3px; padding: 4px 8px; font-size: 12px; }"
                              "QPushButton:hover { background: #7a3d3d; }");
    btnBar->addWidget(addBtn);
    btnBar->addWidget(removeBtn);
    leftLayout->addLayout(btnBar);

    // 右侧: QCustomPlot (多轴堆叠)
    auto *cursorPlot = new CursorPlot(m_splitter);
    m_plot = cursorPlot;
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setSelectionRectMode(QCP::srmNone);  // 默认拖拽模式, 非选区缩放
    m_plot->setAntialiasedElements(QCP::aeAll);
    // 清除默认 axisRect，后面按信号数量动态创建
    m_plot->plotLayout()->clear();

    // 深色画布背景（CANoe 风格）
    m_plot->setBackground(QColor(0x1e, 0x1e, 0x1e));

    m_splitter->addWidget(leftWidget);
    m_splitter->addWidget(m_plot);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 700});

    // ---- 卡尺信息面板（底部，可隐藏） ----
    m_cursorInfoLabel = new QLabel(this);
    m_cursorInfoLabel->setObjectName("CursorInfoLabel");
    m_cursorInfoLabel->setStyleSheet(
        "QLabel { padding: 4px 8px; background: #1e1e1e; color: #cccccc; "
        "border-top: 1px solid #333; font-family: Consolas, monospace; font-size: 12px; }");
    m_cursorInfoLabel->setVisible(false);

    // ---- 底部状态栏 ----
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        "QLabel { padding: 3px 8px; background: #252525; color: #999; "
        "border-top: 1px solid #333; font-family: Consolas, monospace; font-size: 11px; }");
    m_statusLabel->setText("就绪 — 请添加信号或拖入文件");

    mainLayout->addWidget(m_splitter, 1);
    mainLayout->addWidget(m_cursorInfoLabel);
    mainLayout->addWidget(m_statusLabel);

    // ---- 信号添加/删除 ----
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        SignalConfigDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            Signal sig;
            sig.name = dlg.signalName();
            sig.canId = dlg.canId();
            sig.extended = dlg.isExtended();
            sig.dbcSig.name = sig.name;
            sig.dbcSig.startBit = dlg.byteOffset() * 8;
            sig.dbcSig.bitLength = dlg.bitLength();
            sig.dbcSig.littleEndian = !dlg.isBigEndian();
            addSignal(sig);
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        int row = m_signalTree->indexOfTopLevelItem(m_signalTree->currentItem());
        if (row >= 0 && row < m_signals.size())
            removeSignal(row);
    });

    // ---- 缩放 ----
    connect(zoomInBtn, &QToolButton::clicked, this, [this]() {
        for (auto &sd : m_signals) {
            if (sd.axisRect) {
                sd.axisRect->axis(QCPAxis::atBottom)->scaleRange(0.5,
                    sd.axisRect->axis(QCPAxis::atBottom)->range().center());
                sd.yAxis->scaleRange(0.5, sd.yAxis->range().center());
            }
        }
        m_plot->replot();
    });
    connect(zoomOutBtn, &QToolButton::clicked, this, [this]() {
        for (auto &sd : m_signals) {
            if (sd.axisRect) {
                sd.axisRect->axis(QCPAxis::atBottom)->scaleRange(2.0,
                    sd.axisRect->axis(QCPAxis::atBottom)->range().center());
                sd.yAxis->scaleRange(2.0, sd.yAxis->range().center());
            }
        }
        m_plot->replot();
    });
    connect(fitBtn, &QToolButton::clicked, this, [this]() { fitAll(); });
    connect(clearDataBtn, &QToolButton::clicked, this, [this]() { clearData(); });
    connect(exportBtn, &QToolButton::clicked, this, [this]() { exportPlot(); });

    // ---- 暂停/继续 ----
    connect(m_pauseBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_paused = checked;
        m_pauseBtn->setText(checked ? "▶" : "⏸");
        m_pauseBtn->setToolTip(checked ? "继续采集" : "暂停采集");
        updateStatusBar();
    });

    // ---- 时间窗口 ----
    connect(m_timeWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        int sec = m_timeWindowCombo->itemData(idx).toInt();
        if (sec > 0) {
            m_timeWindow = sec;
            refreshTimeAxis();
            updateStatusBar();
        }
    });

    // ---- 采样点开关 ----
    connect(m_pointsToggle, &QCheckBox::toggled, this, [this](bool on) {
        m_showPoints = on;
        for (auto &sd : m_signals) {
            if (sd.graph) {
                if (on) {
                    sd.graph->setScatterStyle(
                        QCPScatterStyle(QCPScatterStyle::ssCircle, sd.config.color, 3));
                } else {
                    sd.graph->setScatterStyle(QCPScatterStyle::ssNone);
                }
            }
        }
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

    // ---- 信号列表右键菜单 ----
    m_signalTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_signalTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_signalTree->itemAt(pos);
        if (!item) return;
        int row = m_signalTree->indexOfTopLevelItem(item);
        QMenu menu(this);
        menu.setStyleSheet(
            "QMenu { background: #2d2d2d; color: #ccc; border: 1px solid #555; padding: 4px; }"
            "QMenu::item { padding: 4px 20px; }"
            "QMenu::item:selected { background: #0c5d8f; }"
            "QMenu::separator { height: 1px; background: #444; margin: 4px 8px; }");
        auto *colorAction = menu.addAction("更改颜色...");
        menu.addSeparator();
        auto *fitAction = menu.addAction("Y 轴适应");
        auto *clrAction = menu.addAction("清空数据");
        menu.addSeparator();
        auto *rmAction = menu.addAction("删除信号");
        auto *sel = menu.exec(m_signalTree->mapToGlobal(pos));
        if (sel == rmAction) {
            removeSignal(row);
        } else if (sel == clrAction) {
            if (row >= 0 && row < m_signals.size() && m_signals[row].graph)
                m_signals[row].graph->data()->clear();
            m_plot->replot();
        } else if (sel == fitAction) {
            if (row >= 0 && row < m_signals.size() && m_signals[row].graph) {
                m_signals[row].graph->rescaleValueAxis(true);
                m_plot->replot();
            }
        } else if (sel == colorAction) {
            if (row >= 0 && row < m_signals.size()) {
                QColor newColor = QColorDialog::getColor(
                    m_signals[row].config.color, this, "选择信号颜色");
                if (newColor.isValid()) {
                    m_signals[row].config.color = newColor;
                    if (m_signals[row].graph)
                        m_signals[row].graph->setPen(QPen(newColor, 1.5));
                    if (m_signals[row].yAxis) {
                        m_signals[row].yAxis->setBasePen(QPen(newColor, 1));
                        m_signals[row].yAxis->setTickPen(QPen(newColor, 1));
                        m_signals[row].yAxis->setSubTickPen(QPen(newColor.darker(150), 1));
                        m_signals[row].yAxis->setTickLabelColor(newColor);
                        m_signals[row].yAxis->setLabelColor(newColor);
                    }
                    if (m_signals[row].nameLabel)
                        m_signals[row].nameLabel->setColor(newColor);
                    updateSignalList();
                    m_plot->replot();
                }
            }
        }
    });

    // ---- 信号列表 checkbox → show/hide ----
    connect(m_signalTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item) {
        int row = m_signalTree->indexOfTopLevelItem(item);
        if (row >= 0 && row < m_signals.size()) {
            bool visible = (item->checkState(0) == Qt::Checked);
            m_signals[row].graph->setVisible(visible);
            if (m_signals[row].axisRect)
                m_signals[row].axisRect->setVisible(visible);
            layoutAxisRects();
            m_plot->replot();
        }
    });

    // ---- 鼠标滚轮缩放 (同步所有 axisRect 的 X 轴) ----
    cursorPlot->onWheel = [this](QWheelEvent *event) {
        // 找到鼠标位置对应的 axisRect
        QPoint pos = event->position().toPoint();
        QCPAxisRect *targetAr = nullptr;
        for (auto &sd : m_signals) {
            if (sd.axisRect && sd.axisRect->visible() && sd.axisRect->rect().contains(pos)) {
                targetAr = sd.axisRect;
                break;
            }
        }
        if (!targetAr) return;

        double factor = (event->angleDelta().y() > 0) ? 0.8 : 1.25;
        QCPAxis *xAxis = targetAr->axis(QCPAxis::atBottom);

        // 以鼠标位置为中心缩放
        double center = xAxis->pixelToCoord(pos.x());
        double newRange = xAxis->range().size() * factor;
        QCPRange newXR(center - newRange / 2, center + newRange / 2);

        // 同步到所有 axisRect 的 X 轴
        for (auto &sd : m_signals) {
            if (sd.axisRect && sd.axisRect->visible()) {
                auto *xa = sd.axisRect->axis(QCPAxis::atBottom);
                QSignalBlocker blocker(xa);
                xa->setRange(newXR);
            }
        }

        // Y 轴也缩放（仅目标 axisRect）
        QCPAxis *yAxis = targetAr->axis(QCPAxis::atLeft);
        double yCenter = yAxis->pixelToCoord(pos.y());
        double newYRange = yAxis->range().size() * factor;
        yAxis->setRange(yCenter - newYRange / 2, yCenter + newYRange / 2);

        m_plot->replot();
        event->accept();
    };

    // ---- 右键菜单 (波形区) ----
    cursorPlot->onContextMenu = [this](QContextMenuEvent *event) {
        QMenu menu(this);
        auto *fitAction = menu.addAction("适应窗口");
        menu.addSeparator();
        auto *togglePoints = menu.addAction(m_showPoints ? "隐藏采样点" : "显示采样点");
        menu.addSeparator();
        auto *singleCursorAction = menu.addAction("单卡尺");
        singleCursorAction->setCheckable(true);
        singleCursorAction->setChecked(m_cursorMode == CursorMode::Single);
        auto *doubleCursorAction = menu.addAction("双卡尺");
        doubleCursorAction->setCheckable(true);
        doubleCursorAction->setChecked(m_cursorMode == CursorMode::Double);
        auto *clearCursorAction = menu.addAction("清除卡尺");
        menu.addSeparator();
        auto *exportAction = menu.addAction("导出图片...");

        auto *sel = menu.exec(event->globalPos());
        if (sel == fitAction) {
            fitAll();
        } else if (sel == togglePoints) {
            m_pointsToggle->setChecked(!m_showPoints);
        } else if (sel == singleCursorAction) {
            m_cursorSingleBtn->setChecked(true);
            m_cursorSingleBtn->click();
        } else if (sel == doubleCursorAction) {
            m_cursorDoubleBtn->setChecked(true);
            m_cursorDoubleBtn->click();
        } else if (sel == clearCursorAction) {
            m_cursorClearBtn->click();
        } else if (sel == exportAction) {
            exportPlot();
        }
    };

    // ---- 卡尺拖动 ----
    auto getPrimaryXAxis = [this]() -> QCPAxis* {
        for (auto &sd : m_signals) {
            if (sd.axisRect && sd.axisRect->visible())
                return sd.axisRect->axis(QCPAxis::atBottom);
        }
        return nullptr;
    };

    cursorPlot->onMousePress = [this, getPrimaryXAxis](QMouseEvent *event) {
        if (m_cursorMode == CursorMode::None) return;
        if (event->button() != Qt::LeftButton) return;

        QCPAxis *xAxis = getPrimaryXAxis();
        if (!xAxis) return;

        double x = xAxis->pixelToCoord(event->pos().x());
        double tolerance = (xAxis->range().size()) / 50.0;

        if (m_cursorMode == CursorMode::Double && m_cursor2) {
            if (std::abs(x - m_cursor2Time) < tolerance) {
                m_draggingCursor = 2;
                event->accept();
                return;
            }
        }
        if (m_cursor1 && std::abs(x - m_cursor1Time) < tolerance) {
            m_draggingCursor = 1;
            event->accept();
            return;
        }
        if (m_cursorMode == CursorMode::Single) {
            moveCursor(1, x);
            m_draggingCursor = 1;
            event->accept();
        } else if (m_cursorMode == CursorMode::Double) {
            if (m_cursor2 && std::abs(x - m_cursor2Time) < std::abs(x - m_cursor1Time)) {
                moveCursor(2, x);
                m_draggingCursor = 2;
            } else {
                moveCursor(1, x);
                m_draggingCursor = 1;
            }
            event->accept();
        }
    };

    cursorPlot->onMouseMove = [this, getPrimaryXAxis](QMouseEvent *event) {
        if (m_draggingCursor == 0) return;
        QCPAxis *xAxis = getPrimaryXAxis();
        if (!xAxis) return;
        double x = xAxis->pixelToCoord(event->pos().x());
        moveCursor(m_draggingCursor, x);
        event->accept();
    };

    cursorPlot->onMouseRelease = [this](QMouseEvent *event) {
        m_draggingCursor = 0;
        event->accept();
    };
}

// ============================================================
//  样式配置
// ============================================================

void GraphicView::styleAxisRect(QCPAxisRect *ar, const QColor &color, const QString &name)
{
    // 网格
    ar->axis(QCPAxis::atBottom)->grid()->setVisible(true);
    ar->axis(QCPAxis::atBottom)->grid()->setPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1, Qt::DotLine));
    ar->axis(QCPAxis::atLeft)->grid()->setVisible(true);
    ar->axis(QCPAxis::atLeft)->grid()->setPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1, Qt::DotLine));

    // 子网格
    ar->axis(QCPAxis::atBottom)->grid()->setSubGridVisible(true);
    ar->axis(QCPAxis::atBottom)->grid()->setSubGridPen(QPen(QColor(0x2a, 0x2a, 0x2a), 1, Qt::DotLine));
    ar->axis(QCPAxis::atLeft)->grid()->setSubGridVisible(true);
    ar->axis(QCPAxis::atLeft)->grid()->setSubGridPen(QPen(QColor(0x2a, 0x2a, 0x2a), 1, Qt::DotLine));

    // 信号色微染背景 (CANoe 风格：每行有淡淡的信号色调)
    ar->setBackground(QBrush(QColor(
        color.red() * 0.08 + 0x1e * 0.92,
        color.green() * 0.08 + 0x1e * 0.92,
        color.blue() * 0.08 + 0x1e * 0.92)));

    // X 轴样式 — 使用 TimeTicker (mm:ss.ms 格式)
    auto *xAxis = ar->axis(QCPAxis::atBottom);
    xAxis->setBasePen(QPen(QColor(0x55, 0x55, 0x55), 1));
    xAxis->setTickPen(QPen(QColor(0x55, 0x55, 0x55), 1));
    xAxis->setSubTickPen(QPen(QColor(0x44, 0x44, 0x44), 1));
    xAxis->setTickLabelColor(QColor(0xcc, 0xcc, 0xcc));
    xAxis->setLabelColor(QColor(0xcc, 0xcc, 0xcc));
    xAxis->setTicker(QSharedPointer<TimeTicker>::create());
    xAxis->setRange(0, m_timeWindow);

    // Y 轴样式
    auto *yAxis = ar->axis(QCPAxis::atLeft);
    yAxis->setBasePen(QPen(color, 1));
    yAxis->setTickPen(QPen(color, 1));
    yAxis->setSubTickPen(QPen(color.darker(150), 1));
    yAxis->setTickLabelColor(color);
    yAxis->setLabelColor(color);
    // 标签：信号名 + 单位 (如果 DBC 中有定义)
    yAxis->setLabel(name);

    // 右侧 Y 轴（镜像刻度）
    ar->axis(QCPAxis::atRight)->setVisible(true);
    ar->axis(QCPAxis::atRight)->setTickLabels(false);
    ar->axis(QCPAxis::atRight)->setBasePen(QPen(QColor(0x55, 0x55, 0x55), 1));

    // 顶部 X 轴（镜像刻度）
    ar->axis(QCPAxis::atTop)->setVisible(true);
    ar->axis(QCPAxis::atTop)->setTickLabels(false);
    ar->axis(QCPAxis::atTop)->setBasePen(QPen(QColor(0x55, 0x55, 0x55), 1));

    // 行间分隔线 (顶部边框)
    ar->axis(QCPAxis::atTop)->setTickPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1));
    ar->axis(QCPAxis::atBottom)->setTickPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1));

    // 边距 (行间距: 顶部 4px, 底部 4px — CANoe 风格行分离)
    ar->setMargins(QMargins(60, 4, 60, 4));
}

// ============================================================
//  多轴布局
// ============================================================

void GraphicView::layoutAxisRects()
{
    auto *layout = m_plot->plotLayout();

    QList<QCPLayoutElement*> taken;
    for (int i = layout->elementCount() - 1; i >= 0; --i) {
        if (auto *el = layout->takeAt(i))
            taken.prepend(el);
    }
    layout->simplify();

    int visibleCount = 0;
    for (auto &sd : m_signals) {
        if (sd.axisRect && sd.axisRect->visible()) {
            layout->addElement(visibleCount, 0, sd.axisRect);
            visibleCount++;
        }
    }

    for (auto *el : taken) {
        bool stillUsed = false;
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
        layout->addElement(0, 0, ar);
    }

    m_plot->replot();
}

void GraphicView::addSignal(const Signal &sig)
{
    SignalData sd;
    sd.config = sig;
    if (!sd.config.color.isValid())
        sd.config.color = autoColor(m_signals.size());

    // 创建独立的 axisRect
    sd.axisRect = new QCPAxisRect(m_plot);

    // 样式配置
    styleAxisRect(sd.axisRect, sd.config.color, sig.name);

    sd.yAxis = sd.axisRect->axis(QCPAxis::atLeft);

    // 默认 Y 轴范围
    double yMin = sig.dbcSig.minimum;
    double yMax = sig.dbcSig.maximum;
    if (yMax <= yMin) yMax = yMin + 1.0;
    sd.yAxis->setRange(yMin, yMax);

    // 创建 graph
    sd.graph = m_plot->addGraph(sd.axisRect->axis(QCPAxis::atBottom), sd.yAxis);
    sd.graph->setName(sig.name);
    sd.graph->setPen(QPen(sd.config.color, 1.5));

    // 采样点样式
    if (m_showPoints) {
        sd.graph->setScatterStyle(
            QCPScatterStyle(QCPScatterStyle::ssCircle, sd.config.color, 3));
    }

    // 线条样式：阶梯线（CANoe 风格：值保持到下一个采样点）
    sd.graph->setLineStyle(QCPGraph::lsStepLeft);

    // 信号名叠加文本（CANoe 风格：左上角显示信号名+单位）
    sd.nameLabel = new QCPItemText(m_plot);
    sd.nameLabel->position->setType(QCPItemPosition::ptAxisRectRatio);
    sd.nameLabel->position->setAxisRect(sd.axisRect);
    sd.nameLabel->position->setCoords(0.01, 0.02);
    sd.nameLabel->setPositionAlignment(Qt::AlignLeft | Qt::AlignTop);
    QString labelText = sig.name;
    if (!sig.dbcSig.unit.isEmpty())
        labelText += " [" + sig.dbcSig.unit + "]";
    sd.nameLabel->setText(labelText);
    sd.nameLabel->setColor(sd.config.color);
    sd.nameLabel->setBrush(QBrush(QColor(0, 0, 0, 160)));
    sd.nameLabel->setPadding(QMargins(4, 2, 4, 2));
    QFont labelFont("Consolas", 8);
    labelFont.setBold(true);
    sd.nameLabel->setFont(labelFont);

    // X 轴联动
    QCPAxis *xAxis = sd.axisRect->axis(QCPAxis::atBottom);
    connect(xAxis, static_cast<void(QCPAxis::*)(const QCPRange&)>(&QCPAxis::rangeChanged),
        this, [this](const QCPRange &range) {
        for (auto &s : m_signals) {
            if (s.axisRect) {
                auto *xa = s.axisRect->axis(QCPAxis::atBottom);
                if (xa && xa->range() != range) {
                    QSignalBlocker blocker(xa);
                    xa->setRange(range);
                }
            }
        }
        if (m_cursorMode != CursorMode::None)
            updateCursorValues();
    });

    m_signals.append(sd);
    layoutAxisRects();
    updateSignalList();
    m_plot->replot();
}

void GraphicView::removeSignal(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;

    auto &sd = m_signals[index];
    if (sd.nameLabel)
        m_plot->removeItem(sd.nameLabel);
    if (sd.graph)
        m_plot->removeGraph(sd.graph);
    if (sd.axisRect)
        m_plot->plotLayout()->remove(sd.axisRect);

    m_signals.removeAt(index);
    layoutAxisRects();
    updateSignalList();
    m_plot->replot();
}

void GraphicView::clearSignals()
{
    for (auto &sd : m_signals) {
        if (sd.graph)
            m_plot->removeGraph(sd.graph);
    }
    auto *layout = m_plot->plotLayout();
    for (int i = layout->elementCount() - 1; i >= 0; --i) {
        if (auto *el = layout->takeAt(i))
            delete el;
    }
    m_signals.clear();
    layoutAxisRects();
    updateSignalList();
    m_plot->replot();
}

QVector<GraphicView::Signal> GraphicView::signalConfigs() const
{
    QVector<Signal> result;
    for (const auto &sd : m_signals)
        result.append(sd.config);
    return result;
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
        sd.hasMinMax = false;
        sd.dataMin = 0.0;
        sd.dataMax = 0.0;
    }
    m_currentTime = 0.0;
    refreshTimeAxis();
    m_plot->replot();
    updateStatusBar();
}

void GraphicView::onFrame(const CanFrame &frame)
{
    // 暂停时仅更新当前时间，不添加数据
    if (m_paused) {
        m_currentTime = frame.timestamp;
        return;
    }

    m_currentTime = frame.timestamp;

    bool hasData = false;
    for (auto &sd : m_signals) {
        if ((frame.id & 0x1FFFFFFF) == sd.config.canId &&
            frame.extended == sd.config.extended) {
            double val = extractValue(frame, sd.config);
            if (!std::isnan(val)) {
                sd.graph->addData(frame.timestamp, val);

                // 跟踪 min/max
                if (!sd.hasMinMax) {
                    sd.dataMin = val;
                    sd.dataMax = val;
                    sd.hasMinMax = true;
                } else {
                    if (val < sd.dataMin) sd.dataMin = val;
                    if (val > sd.dataMax) sd.dataMax = val;
                }

                // 裁剪旧数据（超过显示窗口 + 10% 缓冲）
                double cutoff = frame.timestamp - m_timeWindow * 1.1;
                sd.graph->data()->removeBefore(cutoff);

                // 数据量超过上限时降采样
                if (sd.graph->data()->size() > MAX_DISPLAY_POINTS) {
                    // 移除最早 10% 的数据点
                    int removeCount = sd.graph->data()->size() - MAX_DISPLAY_POINTS;
                    auto it = sd.graph->data()->constBegin();
                    for (int i = 0; i < removeCount && it != sd.graph->data()->constEnd(); ++i)
                        ++it;
                    sd.graph->data()->removeBefore(it->key);
                }

                // 自动调整 Y 轴范围（仅在数据超出当前范围时扩展）
                double curMin = sd.yAxis->range().lower;
                double curMax = sd.yAxis->range().upper;
                if (val < curMin) {
                    double range = curMax - curMin;
                    sd.yAxis->setRange(val, curMax + (curMin - val) * 0.1 + range * 0.05);
                }
                if (val > curMax) {
                    double range = curMax - curMin;
                    sd.yAxis->setRange(curMin - (val - curMax) * 0.1 - range * 0.05, val);
                }

                hasData = true;
            }
        }
    }

    if (hasData) {
        // 刷新时间轴范围（不触发 replot，由定时器处理）
        double tEnd = m_currentTime;
        double tStart = std::max(0.0, tEnd - m_timeWindow);
        QCPRange range(tStart, tEnd);
        for (auto &sd : m_signals) {
            if (sd.axisRect) {
                auto *xa = sd.axisRect->axis(QCPAxis::atBottom);
                QSignalBlocker blocker(xa);
                xa->setRange(range);
            }
        }

        // 更新当前时间指示线
        if (m_currentTimeLine) {
            m_currentTimeLine->point1->setCoords(m_currentTime, 0);
            m_currentTimeLine->point2->setCoords(m_currentTime, 1);
        }

        // 标记需要重绘
        m_replotPending = true;
        // 启动定时器（如果未运行）
        if (!m_replotTimer.isActive())
            m_replotTimer.start();
    }
}

void GraphicView::onReplotTimeout()
{
    if (!m_replotPending) return;
    m_replotPending = false;
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
                for (const auto &frame : frames) {
                    m_currentTime = frame.timestamp;
                    for (auto &sd : m_signals) {
                        if ((frame.id & 0x1FFFFFFF) == sd.config.canId &&
                            frame.extended == sd.config.extended) {
                            double val = extractValue(frame, sd.config);
                            if (!std::isnan(val))
                                sd.graph->addData(frame.timestamp, val);
                        }
                    }
                }
                refreshTimeAxis();
                fitAll();
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
    QCPRange range(tStart, tEnd);
    for (auto &sd : m_signals) {
        if (sd.axisRect) {
            auto *xa = sd.axisRect->axis(QCPAxis::atBottom);
            QSignalBlocker blocker(xa);
            xa->setRange(range);
        }
    }
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
        // 列 0: 信号名 + 色块图标 (CANoe 风格)
        QPixmap colorPix(14, 14);
        colorPix.fill(sd.config.color);
        item->setIcon(0, QIcon(colorPix));
        item->setText(0, sd.config.name);
        item->setForeground(0, sd.config.color);
        // 列 1: 原始值
        item->setText(1, "—");
        // 列 2: 物理值
        item->setText(2, "—");
        // 列 3: 单位
        item->setText(3, sd.config.dbcSig.unit);
        // 列 4: Min
        item->setText(4, sd.hasMinMax ? QString::number(sd.dataMin, 'f', 2) : "—");
        // 列 5: Max
        item->setText(5, sd.hasMinMax ? QString::number(sd.dataMax, 'f', 2) : "—");
        // 列 6: ID
        item->setText(6, QString("0x%1").arg(sd.config.canId, 0, 16).toUpper());
        // 列 7: 点数
        item->setText(7, "0");
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, i);
        m_signalTree->addTopLevelItem(item);
    }
    m_signalTree->blockSignals(false);
    updateStatusBar();
}

void GraphicView::updateSignalValues()
{
    for (int i = 0; i < m_signals.size() && i < m_signalTree->topLevelItemCount(); ++i) {
        auto *item = m_signalTree->topLevelItem(i);
        auto &sd = m_signals[i];

        if (!sd.graph || sd.graph->data()->isEmpty()) {
            item->setText(1, "—");
            item->setText(2, "—");
            item->setText(7, "0");
            continue;
        }

        // 获取最后一个数据点
        auto it = std::prev(sd.graph->data()->constEnd());
        double physVal = it->value;

        // 原始值
        double factor = sd.config.dbcSig.factor;
        if (factor == 0) factor = 1.0;
        quint64 rawVal = static_cast<quint64>((physVal - sd.config.dbcSig.offset) / factor + 0.5);

        item->setText(1, QString::number(rawVal));
        item->setText(2, QString::number(physVal, 'f', 3));
        // Min/Max 列也更新
        item->setText(4, sd.hasMinMax ? QString::number(sd.dataMin, 'f', 2) : "—");
        item->setText(5, sd.hasMinMax ? QString::number(sd.dataMax, 'f', 2) : "—");
        item->setText(7, QString::number(sd.graph->data()->size()));
    }
}

void GraphicView::updateCursorValues()
{
    if (m_cursorMode == CursorMode::None) {
        for (int i = 0; i < m_signalTree->topLevelItemCount(); ++i) {
            auto *item = m_signalTree->topLevelItem(i);
            item->setText(1, "—");
            item->setText(2, "—");
        }
        return;
    }

    for (int i = 0; i < m_signals.size() && i < m_signalTree->topLevelItemCount(); ++i) {
        auto *item = m_signalTree->topLevelItem(i);
        auto &sd = m_signals[i];

        double physVal;
        if (sd.graph && valueAtTime(sd.graph, m_cursor1Time, physVal)) {
            double factor = sd.config.dbcSig.factor;
            if (factor == 0) factor = 1.0;
            quint64 rawVal = static_cast<quint64>((physVal - sd.config.dbcSig.offset) / factor + 0.5);
            item->setText(1, QString::number(rawVal));
            item->setText(2, QString::number(physVal, 'f', 3));
        } else {
            item->setText(1, "—");
            item->setText(2, "—");
        }

        if (m_cursorMode == CursorMode::Double && m_cursor2) {
            double physVal2;
            if (sd.graph && valueAtTime(sd.graph, m_cursor2Time, physVal2)) {
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

        for (int i = 0; i < m_signals.size(); ++i) {
            double v1, v2;
            if (m_signals[i].graph &&
                valueAtTime(m_signals[i].graph, m_cursor1Time, v1) &&
                valueAtTime(m_signals[i].graph, m_cursor2Time, v2)) {
                info += QString("  Δ%1 = %2")
                    .arg(m_signals[i].config.name)
                    .arg(v2 - v1, 0, 'f', 3);
            }
        }

        m_cursorInfoLabel->setText(info);
    }
}

// ============================================================
//  卡尺系统
// ============================================================

void GraphicView::ensureCursors()
{
    if (!m_cursor1) {
        m_cursor1 = new QCPItemStraightLine(m_plot);
        m_cursor1->setPen(QPen(QColor(0xE9, 0x1E, 0x63), 1, Qt::DashLine));
        m_cursor1->point1->setCoords(m_cursor1Time, 0);
        m_cursor1->point2->setCoords(m_cursor1Time, 1);
    }
    if (!m_cursor2) {
        m_cursor2 = new QCPItemStraightLine(m_plot);
        m_cursor2->setPen(QPen(QColor(0x00, 0x96, 0x88), 1, Qt::DashLine));
        m_cursor2->point1->setCoords(m_cursor2Time, 0);
        m_cursor2->point2->setCoords(m_cursor2Time, 1);
    }
}

void GraphicView::ensureCurrentTimeLine()
{
    if (!m_currentTimeLine) {
        m_currentTimeLine = new QCPItemStraightLine(m_plot);
        m_currentTimeLine->setPen(QPen(QColor(0xFF, 0xFF, 0x00), 1, Qt::DotLine));
        m_currentTimeLine->point1->setCoords(0, 0);
        m_currentTimeLine->point2->setCoords(0, 1);
    }
}

void GraphicView::setCursorMode(CursorMode mode)
{
    m_cursorMode = mode;

    if (mode == CursorMode::None) {
        if (m_cursor1) { m_plot->removeItem(m_cursor1); m_cursor1 = nullptr; }
        if (m_cursor2) { m_plot->removeItem(m_cursor2); m_cursor2 = nullptr; }
        m_draggingCursor = 0;
        updateCursorValues();
        m_plot->replot();
        return;
    }

    ensureCursors();

    double center = m_currentTime > 0 ? m_currentTime - m_timeWindow / 2 : 0;
    if (mode == CursorMode::Single) {
        m_cursor1->setVisible(true);
        m_cursor2->setVisible(false);
        m_cursor1Time = center;
        moveCursor(1, center);
    } else if (mode == CursorMode::Double) {
        m_cursor1->setVisible(true);
        m_cursor2->setVisible(true);
        m_cursor1Time = center - m_timeWindow * 0.1;
        m_cursor2Time = center + m_timeWindow * 0.1;
        moveCursor(1, m_cursor1Time);
        moveCursor(2, m_cursor2Time);
    }

    updateCursorValues();
    m_plot->replot();
}

void GraphicView::moveCursor(int which, double time)
{
    if (which == 1 && m_cursor1) {
        m_cursor1Time = time;
        m_cursor1->point1->setCoords(time, 0);
        m_cursor1->point2->setCoords(time, 1);
    } else if (which == 2 && m_cursor2) {
        m_cursor2Time = time;
        m_cursor2->point1->setCoords(time, 0);
        m_cursor2->point2->setCoords(time, 1);
    }
    updateCursorValues();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

bool GraphicView::valueAtTime(QCPGraph *graph, double time, double &outVal) const
{
    if (!graph || graph->data()->size() == 0)
        return false;

    auto it = graph->data()->findBegin(time);
    if (it == graph->data()->end())
        return false;

    if (it == graph->data()->begin()) {
        if (it->key >= time) {
            outVal = it->value;
            return true;
        }
    }

    if (it != graph->data()->begin()) {
        auto prev = std::prev(it);
        double t0 = prev->key;
        double t1 = it->key;
        double v0 = prev->value;
        double v1 = it->value;
        if (t1 == t0) {
            outVal = v1;
        } else {
            double ratio = (time - t0) / (t1 - t0);
            outVal = v0 + ratio * (v1 - v0);
        }
        return true;
    }

    outVal = it->value;
    return true;
}

// ============================================================
//  适应窗口 & 导出
// ============================================================

void GraphicView::fitAll()
{
    double tEnd = m_currentTime;
    double tStart = std::max(0.0, tEnd - m_timeWindow);
    QCPRange range(tStart, tEnd);
    for (auto &sd : m_signals) {
        if (sd.axisRect) {
            auto *xa = sd.axisRect->axis(QCPAxis::atBottom);
            QSignalBlocker blocker(xa);
            xa->setRange(range);
        }
        if (sd.graph)
            sd.graph->rescaleValueAxis(true);
    }
    m_plot->replot();
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
    parts << QString("信号: %1").arg(m_signals.size());

    int totalPoints = 0;
    for (const auto &sd : m_signals) {
        if (sd.graph)
            totalPoints += sd.graph->data()->size();
    }
    parts << QString("采样点: %1").arg(totalPoints);
    parts << QString("时间: %1").arg(formatTime(m_currentTime));
    parts << QString("窗口: %1s").arg(m_timeWindow);

    if (m_paused)
        parts << "[已暂停]";

    m_statusLabel->setText(parts.join("  |  "));
}
