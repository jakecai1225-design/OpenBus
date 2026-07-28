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

#include <QChart>
#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>

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

    // ---- Splitter: signal list | chart view ----
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

    // Right panel: chart view
    m_chart = new QChart();
    m_chart->setTitle("信号波形");
    m_chart->setTheme(QChart::ChartThemeLight);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->setMargins(QMargins(2, 2, 2, 2));

    m_timeAxis = new QValueAxis();
    m_timeAxis->setTitleText("时间 (s)");
    m_timeAxis->setLabelFormat("%.1f");
    m_timeAxis->setRange(0, m_timeWindow);
    m_chart->addAxis(m_timeAxis, Qt::AlignBottom);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setRubberBand(QChartView::RectangleRubberBand);

    m_splitter->addWidget(leftWidget);
    m_splitter->addWidget(m_chartView);
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
            sig.byteOffset = dlg.byteOffset();
            sig.bitLength = dlg.bitLength();
            sig.bigEndian = dlg.isBigEndian();
            addSignal(sig);
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        int row = m_signalList->currentRow();
        if (row >= 0 && row < m_signals.size())
            removeSignal(row);
    });

    connect(zoomInBtn, &QToolButton::clicked, this, [this]() {
        m_chart->zoomIn();
    });
    connect(zoomOutBtn, &QToolButton::clicked, this, [this]() {
        m_chart->zoomOut();
    });
    connect(fitBtn, &QToolButton::clicked, this, [this]() {
        m_chart->zoomReset();
        refreshTimeAxis();
    });

    // Signal list checkbox → show/hide signal
    connect(m_signalList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        int row = m_signalList->row(item);
        if (row >= 0 && row < m_signals.size()) {
            bool visible = (item->checkState() == Qt::Checked);
            m_signals[row].series->setVisible(visible);
            m_signals[row].yAxis->setVisible(visible);
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

    // Create line series
    sd.series = new QLineSeries();
    sd.series->setName(sig.name);
    sd.series->setColor(sd.config.color);
    m_chart->addSeries(sd.series);

    // Create Y axis (alternating left/right)
    sd.yAxis = new QValueAxis();
    sd.yAxis->setLabelFormat("%.0f");
    sd.yAxis->setTitleText(sig.name);
    // Default range based on bit length
    double yMax = (sig.bitLength >= 32) ? 0xFFFFFFFF : static_cast<double>((1ULL << sig.bitLength) - 1);
    sd.yAxis->setRange(0, yMax);

    Qt::Alignment align = (m_signals.size() % 2 == 0) ? Qt::AlignLeft : Qt::AlignRight;
    m_chart->addAxis(sd.yAxis, align);
    sd.series->attachAxis(m_timeAxis);
    sd.series->attachAxis(sd.yAxis);

    m_signals.append(sd);
    updateSignalList();
}

void GraphicView::removeSignal(int index)
{
    if (index < 0 || index >= m_signals.size())
        return;

    auto &sd = m_signals[index];
    if (sd.series) {
        m_chart->removeSeries(sd.series);
        sd.series->deleteLater();
    }
    if (sd.yAxis) {
        m_chart->removeAxis(sd.yAxis);
        sd.yAxis->deleteLater();
    }

    m_signals.removeAt(index);
    updateSignalList();
}

void GraphicView::clearSignals()
{
    for (auto &sd : m_signals) {
        if (sd.series) {
            m_chart->removeSeries(sd.series);
            sd.series->deleteLater();
        }
        if (sd.yAxis) {
            m_chart->removeAxis(sd.yAxis);
            sd.yAxis->deleteLater();
        }
    }
    m_signals.clear();
    updateSignalList();
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
        if (sd.series)
            sd.series->clear();
    }
    m_currentTime = 0.0;
    refreshTimeAxis();
}

void GraphicView::onFrame(const CanFrame &frame)
{
    m_currentTime = frame.timestamp;

    for (auto &sd : m_signals) {
        if ((frame.id & 0x1FFFFFFF) == sd.config.canId &&
            frame.extended == sd.config.extended) {
            double val = extractValue(frame, sd.config);
            if (!std::isnan(val)) {
                sd.series->append(frame.timestamp, val);

                // Trim old points outside the time window
                double cutoff = frame.timestamp - m_timeWindow;
                int removeCount = 0;
                while (removeCount < sd.series->count() &&
                       sd.series->at(removeCount).x() < cutoff) {
                    ++removeCount;
                }
                if (removeCount > 0)
                    sd.series->removePoints(0, removeCount);

                // Auto-range Y axis (lightweight: check new value only)
                if (sd.series->count() > 0) {
                    double curMin = sd.yAxis->min();
                    double curMax = sd.yAxis->max();
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
    m_timeAxis->setRange(tStart, tEnd);
}
