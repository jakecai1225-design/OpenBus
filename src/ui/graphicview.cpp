#include "graphicview.h"
#include "signalconfigdialog.h"

#include <QSplitter>
#include <QListWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <cmath>
#include <algorithm>

#include <qcustomplot.h>

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

    // ---- Toolbar (zoom / fit / measure) ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    auto *zoomInBtn = new QToolButton(this);
    zoomInBtn->setText("🔍+");
    zoomInBtn->setToolTip("放大");
    auto *zoomOutBtn = new QToolButton(this);
    zoomOutBtn->setText("🔍-");
    zoomOutBtn->setToolTip("缩小");
    auto *fitBtn = new QToolButton(this);
    fitBtn->setText("⤢");
    fitBtn->setToolTip("适应窗口");
    auto *measureBtn = new QToolButton(this);
    measureBtn->setText("📏");
    measureBtn->setToolTip("测量 (待实现)");
    measureBtn->setEnabled(false);

    m_toolbar->addWidget(zoomInBtn);
    m_toolbar->addWidget(zoomOutBtn);
    m_toolbar->addWidget(fitBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(measureBtn);
    mainLayout->addWidget(m_toolbar);

    // ---- Splitter: signal list | plot ----
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel: signal list
    auto *leftWidget = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_signalList = new QListWidget(leftWidget);
    m_signalList->setMinimumWidth(180);
    leftLayout->addWidget(m_signalList, 1);

    auto *btnBar = new QHBoxLayout;
    btnBar->setContentsMargins(4, 4, 4, 4);
    auto *addBtn = new QPushButton("+ 添加信号", leftWidget);
    auto *removeBtn = new QPushButton("- 删除信号", leftWidget);
    btnBar->addWidget(addBtn);
    btnBar->addWidget(removeBtn);
    leftLayout->addLayout(btnBar);

    // Right panel: QCustomPlot
    m_plot = new QCustomPlot(this);
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->setSelectionRectMode(QCP::srmZoom);

    // Configure X axis (time)
    m_plot->xAxis->setLabel("时间 (s)");
    m_plot->xAxis->setNumberFormat("f");
    m_plot->xAxis->setNumberPrecision(1);
    m_plot->xAxis->setRange(0, m_timeWindow);

    // Default Y axis (will be hidden when signals have their own)
    m_plot->yAxis->setLabel("值");
    m_plot->yAxis->setRange(0, 1);

    // Legend at bottom
    m_plot->legend->setVisible(true);
    m_plot->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignBottom | Qt::AlignHCenter);

    // Antialiasing
    m_plot->setAntialiasedElements(QCP::aeAll);

    m_splitter->addWidget(leftWidget);
    m_splitter->addWidget(m_plot);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({180, 500});

    mainLayout->addWidget(m_splitter, 1);

    // ---- Connections ----
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        SignalConfigDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            Signal sig;
            sig.name = dlg.signalName();
            sig.canId = dlg.canId();
            sig.extended = dlg.isExtended();
            // 构造 DbcSignal 定义
            sig.dbcSig.name = sig.name;
            sig.dbcSig.startBit = dlg.byteOffset() * 8;
            sig.dbcSig.bitLength = dlg.bitLength();
            sig.dbcSig.littleEndian = !dlg.isBigEndian();
            sig.dbcSig.factor = 1.0;
            sig.dbcSig.offset = 0.0;
            addSignal(sig);
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        int row = m_signalList->currentRow();
        if (row >= 0 && row < m_signals.size())
            removeSignal(row);
    });

    connect(zoomInBtn, &QToolButton::clicked, this, [this]() {
        m_plot->xAxis->scaleRange(0.5, m_plot->xAxis->range().center());
        m_plot->yAxis->scaleRange(0.5, m_plot->yAxis->range().center());
        m_plot->replot();
    });
    connect(zoomOutBtn, &QToolButton::clicked, this, [this]() {
        m_plot->xAxis->scaleRange(2.0, m_plot->xAxis->range().center());
        m_plot->yAxis->scaleRange(2.0, m_plot->yAxis->range().center());
        m_plot->replot();
    });
    connect(fitBtn, &QToolButton::clicked, this, [this]() {
        m_plot->rescaleAxes(true);
        refreshTimeAxis();
        m_plot->replot(QCustomPlot::rpQueuedReplot);
    });

    // Signal list checkbox → show/hide signal
    connect(m_signalList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        int row = m_signalList->row(item);
        if (row >= 0 && row < m_signals.size()) {
            bool visible = (item->checkState() == Qt::Checked);
            m_signals[row].graph->setVisible(visible);
            m_signals[row].yAxis->setVisible(visible);
            m_plot->replot(QCustomPlot::rpQueuedReplot);
        }
    });

    // Context menu on signal list
    m_signalList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_signalList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_signalList->itemAt(pos);
        if (!item) return;
        int row = m_signalList->row(item);
        QMenu menu(this);
        auto *rmAction = menu.addAction("删除信号");
        auto *sel = menu.exec(m_signalList->mapToGlobal(pos));
        if (sel == rmAction)
            removeSignal(row);
    });
}

void GraphicView::addSignal(const Signal &sig)
{
    SignalData sd;
    sd.config = sig;
    if (!sd.config.color.isValid())
        sd.config.color = autoColor(m_signals.size());

    // Create graph
    sd.graph = m_plot->addGraph();
    sd.graph->setName(sig.name);
    sd.graph->setPen(QPen(sd.config.color));
    sd.graph->setKeyAxis(m_plot->xAxis);

    // Create Y axis (alternating left/right)
    QCPAxis::AxisType axisType = (m_signals.size() % 2 == 0)
        ? QCPAxis::atLeft : QCPAxis::atRight;
    sd.yAxis = m_plot->axisRect()->addAxis(axisType);
    sd.yAxis->setLabel(sig.name);
    sd.yAxis->setNumberFormat("g");
    sd.yAxis->setNumberPrecision(3);

    // Default range based on DBC signal min/max
    double yMin = sig.dbcSig.minimum;
    double yMax = sig.dbcSig.maximum;
    if (yMax <= yMin) yMax = yMin + 1.0;
    sd.yAxis->setRange(yMin, yMax);

    sd.graph->setValueAxis(sd.yAxis);

    m_signals.append(sd);
    updateSignalList();

    // Hide the default Y axis if we have custom ones
    if (m_signals.size() == 1)
        m_plot->yAxis->setVisible(false);

    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void GraphicView::removeSignal(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;

    auto &sd = m_signals[index];
    if (sd.graph)
        m_plot->removeGraph(sd.graph);
    if (sd.yAxis)
        m_plot->axisRect()->removeAxis(sd.yAxis);

    m_signals.removeAt(index);

    // Show default Y axis if no signals remain
    if (m_signals.isEmpty())
        m_plot->yAxis->setVisible(true);

    updateSignalList();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

void GraphicView::clearSignals()
{
    for (auto &sd : m_signals) {
        if (sd.graph)
            m_plot->removeGraph(sd.graph);
        if (sd.yAxis)
            m_plot->axisRect()->removeAxis(sd.yAxis);
    }
    m_signals.clear();

    m_plot->yAxis->setVisible(true);
    updateSignalList();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
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
    for (auto &sd : m_signals) {
        if (sd.graph)
            sd.graph->data()->clear();
    }
    m_currentTime = 0.0;
    refreshTimeAxis();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
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

                // Trim old points outside the time window
                double cutoff = frame.timestamp - m_timeWindow;
                sd.graph->data()->removeBefore(cutoff);

                // Auto-range Y axis (lightweight: check new value only)
                QCPRange range = sd.yAxis->range();
                double curMin = range.lower;
                double curMax = range.upper;
                bool needUpdate = false;
                if (val < curMin) { curMin = val; needUpdate = true; }
                if (val > curMax) { curMax = val; needUpdate = true; }
                if (needUpdate) {
                    double rangeSpan = curMax - curMin;
                    if (rangeSpan < 1) { curMin -= 1; curMax += 1; }
                    sd.yAxis->setRange(curMin, curMax);
                }
            }
        }
    }

    refreshTimeAxis();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
}

double GraphicView::extractValue(const CanFrame &frame, const Signal &sig) const
{
    // 使用 DBC 信号定义进行精确解码（支持任意位起始/长度/factor/offset/signed/float）
    return sig.dbcSig.decode(frame.data);
}

void GraphicView::updateSignalList()
{
    m_signalList->blockSignals(true);
    m_signalList->clear();

    for (int i = 0; i < m_signals.size(); ++i) {
        const auto &sd = m_signals[i];
        QString text = QString("● %1  (0x%2)")
            .arg(sd.config.name)
            .arg(sd.config.canId, 0, 16).toUpper();

        auto *item = new QListWidgetItem(text);
        item->setCheckState(Qt::Checked);
        // Set color for the bullet
        QColor c = sd.config.color;
        item->setForeground(c);
        m_signalList->addItem(item);
    }
    m_signalList->blockSignals(false);
}

void GraphicView::refreshTimeAxis()
{
    double tEnd = m_currentTime;
    double tStart = std::max(0.0, tEnd - m_timeWindow);
    m_plot->xAxis->setRange(tStart, tEnd);
}
