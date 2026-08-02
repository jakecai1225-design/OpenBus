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
#include <QMenu>
#include <QAction>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <cmath>
#include <algorithm>
#include <functional>

#include "qcustomplot.h"

// ============================================================
//  辅助：QCustomPlot 子类 — 支持卡尺拖动
// ============================================================

class CursorPlot : public QCustomPlot
{
public:
    explicit CursorPlot(QWidget *parent = nullptr) : QCustomPlot(parent) {}

    std::function<void(QMouseEvent*)> onMousePress;
    std::function<void(QMouseEvent*)> onMouseMove;
    std::function<void(QMouseEvent*)> onMouseRelease;

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
};

// ============================================================
//  GraphicView 实现
// ============================================================

GraphicView::GraphicView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
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

    auto *zoomInBtn = new QToolButton(m_toolbar);
    zoomInBtn->setText("🔍+");
    zoomInBtn->setToolTip("放大");
    auto *zoomOutBtn = new QToolButton(m_toolbar);
    zoomOutBtn->setText("🔍-");
    zoomOutBtn->setToolTip("缩小");
    auto *fitBtn = new QToolButton(m_toolbar);
    fitBtn->setText("📐");
    fitBtn->setToolTip("适应窗口");

    m_cursorSingleBtn = new QToolButton(m_toolbar);
    m_cursorSingleBtn->setText("┊");
    m_cursorSingleBtn->setToolTip("单卡尺");
    m_cursorSingleBtn->setCheckable(true);
    m_cursorSingleBtn->setMinimumWidth(28);

    m_cursorDoubleBtn = new QToolButton(m_toolbar);
    m_cursorDoubleBtn->setText("┊┊");
    m_cursorDoubleBtn->setToolTip("双卡尺");
    m_cursorDoubleBtn->setCheckable(true);
    m_cursorDoubleBtn->setMinimumWidth(36);

    m_cursorClearBtn = new QToolButton(m_toolbar);
    m_cursorClearBtn->setText("✕");
    m_cursorClearBtn->setToolTip("清除卡尺");
    m_cursorClearBtn->setMinimumWidth(28);

    m_toolbar->addWidget(zoomInBtn);
    m_toolbar->addWidget(zoomOutBtn);
    m_toolbar->addWidget(fitBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_cursorSingleBtn);
    m_toolbar->addWidget(m_cursorDoubleBtn);
    m_toolbar->addWidget(m_cursorClearBtn);
    mainLayout->addWidget(m_toolbar);

    // ---- 分割器: 信号列表 | 波形区 ----
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // 左侧: 信号列表 (QTreeWidget)
    auto *leftWidget = new QWidget(m_splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_signalTree = new QTreeWidget(leftWidget);
    m_signalTree->setColumnCount(5);
    m_signalTree->setHeaderLabels({"信号", "原始值", "物理值", "单位", "ID"});
    m_signalTree->setRootIsDecorated(false);
    m_signalTree->setAlternatingRowColors(true);
    m_signalTree->setMinimumWidth(280);
    m_signalTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_signalTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_signalTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_signalTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_signalTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    leftLayout->addWidget(m_signalTree, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *addBtn = new QPushButton("+ 添加信号", leftWidget);
    auto *removeBtn = new QPushButton("- 删除信号", leftWidget);
    btnBar->addWidget(addBtn);
    btnBar->addWidget(removeBtn);
    leftLayout->addLayout(btnBar);

    // 右侧: QCustomPlot (多轴堆叠)
    auto *cursorPlot = new CursorPlot(m_splitter);
    m_plot = cursorPlot;
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setSelectionRectMode(QCP::srmZoom);
    m_plot->setAntialiasedElements(QCP::aeAll);
    // 清除默认 axisRect，后面按信号数量动态创建
    // 使用 clear() 而非 while+takeAt：takeAt 只置空 cell 不缩小 grid，
    // elementCount() 返回 rowCount*columnCount 仍 > 0，会导致死循环。
    // clear() 会检查 elementAt(i) 非空才移除，并调用 simplify() 收缩 grid。
    m_plot->plotLayout()->clear();

    m_splitter->addWidget(leftWidget);
    m_splitter->addWidget(m_plot);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 600});

    mainLayout->addWidget(m_splitter, 1);

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
    connect(fitBtn, &QToolButton::clicked, this, [this]() {
        refreshTimeAxis();
        for (auto &sd : m_signals) {
            if (sd.graph)
                sd.graph->rescaleValueAxis(true);
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
        auto *rmAction = menu.addAction("删除信号");
        auto *sel = menu.exec(m_signalTree->mapToGlobal(pos));
        if (sel == rmAction)
            removeSignal(row);
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

    // ---- 卡尺拖动 ----
    // 辅助 lambda：获取第一个可见信号的 X 轴（不能用 m_plot->xAxis，默认 axisRect 已删除）
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
        // 判断点击在哪个卡尺附近
        double tolerance = (xAxis->range().size()) / 50.0;  // 2% 范围

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
        // 否则移动最近的卡尺（单卡尺模式）或 cursor1（双卡尺模式）
        if (m_cursorMode == CursorMode::Single) {
            moveCursor(1, x);
            m_draggingCursor = 1;
            event->accept();
        } else if (m_cursorMode == CursorMode::Double) {
            // 移动较近的那个
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
//  多轴布局
// ============================================================

void GraphicView::layoutAxisRects()
{
    auto *layout = m_plot->plotLayout();

    // 用 takeAt 逐个取出元素（不删除），切勿用 clear() ——
    // clear() 内部调用 removeAt → takeAt + delete，会删除所有元素，
    // 导致 m_signals 中的 axisRect 指针悬空（use-after-free）。
    QList<QCPLayoutElement*> taken;
    for (int i = layout->elementCount() - 1; i >= 0; --i) {
        if (auto *el = layout->takeAt(i))
            taken.prepend(el);
    }
    layout->simplify();  // 收缩空行空列

    // 重新添加可见信号的 axisRect
    int visibleCount = 0;
    for (auto &sd : m_signals) {
        if (sd.axisRect && sd.axisRect->visible()) {
            layout->addElement(visibleCount, 0, sd.axisRect);
            visibleCount++;
        }
    }

    // 删除未被重新添加的元素（即占位用的空 axisRect）
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

    // 如果没有信号，添加一个空的 axisRect 作为占位
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
    sd.axisRect->setMargins(QMargins(50, 2, 50, 2));

    // X 轴 (时间) — 仅最底部的显示刻度标签
    QCPAxis *xAxis = sd.axisRect->axis(QCPAxis::atBottom);
    xAxis->setNumberFormat("f");
    xAxis->setNumberPrecision(1);
    xAxis->setRange(0, m_timeWindow);

    // Y 轴
    sd.yAxis = sd.axisRect->axis(QCPAxis::atLeft);
    sd.yAxis->setLabel(sig.name);
    sd.yAxis->setLabelColor(sd.config.color);
    sd.yAxis->setTickLabelColor(sd.config.color);

    // 默认 Y 轴范围
    double yMin = sig.dbcSig.minimum;
    double yMax = sig.dbcSig.maximum;
    if (yMax <= yMin) yMax = yMin + 1.0;
    sd.yAxis->setRange(yMin, yMax);

    // 创建 graph
    sd.graph = m_plot->addGraph(sd.axisRect->axis(QCPAxis::atBottom), sd.yAxis);
    sd.graph->setName(sig.name);
    sd.graph->setPen(QPen(sd.config.color, 1.5));

    // X 轴联动：当任一 axisRect 的 X 轴范围变化时，同步所有
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
    if (sd.graph)
        m_plot->removeGraph(sd.graph);
    // axisRect 由 plotLayout 管理，removeElement 后会被删除
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
    // 用 takeAt 逐个取出并删除 axisRect（clear() 也能删除，但这里显式做更安全）
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
//  数据更新
// ============================================================

void GraphicView::clearData()
{
    for (auto &sd : m_signals) {
        if (sd.graph)
            sd.graph->data()->clear();
    }
    m_currentTime = 0.0;
    refreshTimeAxis();
    m_plot->replot();
}

void GraphicView::onFrame(const CanFrame &frame)
{
    m_currentTime = frame.timestamp;

    for (auto &sd : m_signals) {
        if ((frame.id & 0x1FFFFFFF) == sd.config.canId &&
            frame.extended == sd.config.extended) {
            double val = extractValue(frame, sd.config);
            if (!std::isnan(val)) {
                sd.graph->addData(frame.timestamp, val);

                // 裁剪旧数据
                double cutoff = frame.timestamp - m_timeWindow;
                sd.graph->data()->removeBefore(cutoff);

                // 自动调整 Y 轴范围
                if (sd.graph->data()->size() > 0) {
                    double curMin = sd.yAxis->range().lower;
                    double curMax = sd.yAxis->range().upper;
                    bool needUpdate = false;
                    if (val < curMin) { curMin = val; needUpdate = true; }
                    if (val > curMax) { curMax = val; needUpdate = true; }
                    if (needUpdate) {
                        double range = curMax - curMin;
                        if (range < 1) { curMin -= 1; curMax += 1; }
                        sd.yAxis->setRange(curMin, curMax);
                    }
                }
            }
        }
    }

    refreshTimeAxis();
    m_plot->replot(QCustomPlot::rpQueuedReplot);

    // 更新卡尺值
    if (m_cursorMode != CursorMode::None)
        updateCursorValues();
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
        item->setText(0, sd.config.name);
        item->setForeground(0, sd.config.color);
        item->setText(1, "—");
        item->setText(2, "—");
        item->setText(3, sd.config.dbcSig.unit);
        item->setText(4, QString("0x%1").arg(sd.config.canId, 0, 16).toUpper());
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, i);
        m_signalTree->addTopLevelItem(item);
    }
    m_signalTree->blockSignals(false);
}

void GraphicView::updateCursorValues()
{
    if (m_cursorMode == CursorMode::None) {
        // 清空值列
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

        // 卡尺 1 的值
        double physVal;
        if (sd.graph && valueAtTime(sd.graph, m_cursor1Time, physVal)) {
            // 原始值 = (physVal - offset) / factor
            double factor = sd.config.dbcSig.factor;
            if (factor == 0) factor = 1.0;
            quint64 rawVal = static_cast<quint64>((physVal - sd.config.dbcSig.offset) / factor + 0.5);
            item->setText(1, QString::number(rawVal));
            item->setText(2, QString::number(physVal, 'f', 3));
        } else {
            item->setText(1, "—");
            item->setText(2, "—");
        }

        // 双卡尺模式：在物理值列显示 Δ 值
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

    // 初始化卡尺位置
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

    // 找到 >= time 的第一个点
    auto it = graph->data()->findBegin(time);
    if (it == graph->data()->end())
        return false;

    if (it == graph->data()->begin()) {
        // time 在数据范围之前
        if (it->key >= time) {
            outVal = it->value;
            return true;
        }
    }

    // 线性插值
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
