#include "iographview.h"

#include "qcustomplot.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QToolButton>
#include <QLabel>
#include <QtMath>
#include <algorithm>

IOGraphView::IOGraphView(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    auto *bar = new QHBoxLayout;
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Frames / s (all)"), 0);
    m_modeCombo->addItem(tr("Bus load % (all)"), 1);
    m_modeCombo->addItem(tr("Frames / s by CAN ID"), 2);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IOGraphView::onModeChanged);

    auto *clearBtn = new QToolButton(this);
    clearBtn->setText(tr("Clear"));
    connect(clearBtn, &QToolButton::clicked, this, &IOGraphView::onClearClicked);

    m_statusLabel = new QLabel(tr("Waiting for frames…"), this);

    bar->addWidget(m_modeCombo);
    bar->addWidget(clearBtn);
    bar->addStretch();
    bar->addWidget(m_statusLabel);
    root->addLayout(bar);

    m_plot = new QCustomPlot(this);
    m_plot->xAxis->setLabel(tr("Time (s)"));
    m_plot->yAxis->setLabel(tr("Frames / s"));
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    m_plot->legend->setVisible(true);
    m_fpsGraph = m_plot->addGraph();
    m_fpsGraph->setName(tr("FPS"));
    m_fpsGraph->setPen(QPen(QColor(0x2b, 0x8a, 0x3e), 1.5));
    m_loadGraph = m_plot->addGraph();
    m_loadGraph->setName(tr("Load %"));
    m_loadGraph->setPen(QPen(QColor(0xd9, 0x48, 0x0f), 1.5));
    m_loadGraph->setVisible(false);
    root->addWidget(m_plot, 1);

    m_refreshTimer.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, &IOGraphView::onRefreshTimer);
    m_refreshTimer.start();
}

void IOGraphView::onFrame(const CanFrame &frame)
{
    if (m_t0 < 0.0)
        m_t0 = frame.timestamp;

    m_lastTs = frame.timestamp;
    ++m_totalFrames;

    const qint64 bucket = qFloor(frame.timestamp); // 1s buckets
    m_globalBuckets[bucket] += 1;

    if (m_byId) {
        ensureIdSeries(frame.id);
        m_idBuckets[frame.id][bucket] += 1;
    }
}

void IOGraphView::clearData()
{
    m_globalBuckets.clear();
    m_idBuckets.clear();
    m_t0 = -1.0;
    m_lastTs = 0.0;
    m_totalFrames = 0;
    for (auto *g : m_idGraphs)
        m_plot->removeGraph(g);
    m_idGraphs.clear();
    if (m_fpsGraph)
        m_fpsGraph->data()->clear();
    if (m_loadGraph)
        m_loadGraph->data()->clear();
    m_plot->replot(QCustomPlot::rpQueuedReplot);
    m_statusLabel->setText(tr("Waiting for frames…"));
}

void IOGraphView::onClearClicked()
{
    clearData();
}

void IOGraphView::onModeChanged(int)
{
    const int mode = m_modeCombo->currentData().toInt();
    m_byId = (mode == 2);
    const bool showLoad = (mode == 1);
    if (m_fpsGraph)
        m_fpsGraph->setVisible(!showLoad && !m_byId);
    if (m_loadGraph)
        m_loadGraph->setVisible(showLoad);
    for (auto *g : m_idGraphs)
        g->setVisible(m_byId);
    m_plot->yAxis->setLabel(showLoad ? tr("Bus load %") : tr("Frames / s"));
    rebuildPlot();
}

void IOGraphView::ensureIdSeries(quint32 canId)
{
    if (m_idGraphs.contains(canId))
        return;
    auto *g = m_plot->addGraph();
    g->setName(QStringLiteral("0x%1").arg(canId, 0, 16).toUpper());
    // Stable-ish color from id
    const int h = static_cast<int>((canId * 2654435761u) % 360);
    g->setPen(QPen(QColor::fromHsv(h, 180, 220), 1.2));
    g->setVisible(m_byId);
    m_idGraphs.insert(canId, g);
}

void IOGraphView::onRefreshTimer()
{
    if (!isVisible())
        return;
    rebuildPlot();
}

void IOGraphView::rebuildPlot()
{
    const int mode = m_modeCombo ? m_modeCombo->currentData().toInt() : 0;

    if (mode == 0 && m_fpsGraph) {
        m_fpsGraph->data()->clear();
        QList<qint64> keys = m_globalBuckets.keys();
        std::sort(keys.begin(), keys.end());
        for (qint64 b : keys) {
            const double t = static_cast<double>(b) - (m_t0 > 0 ? qFloor(m_t0) : 0);
            m_fpsGraph->addData(t, m_globalBuckets.value(b));
        }
    } else if (mode == 1 && m_loadGraph) {
        m_loadGraph->data()->clear();
        // Approx classic CAN: ~47-bit overhead + 8*dlc data bits; use 100 bits/frame avg
        constexpr double bitsPerFrame = 100.0;
        QList<qint64> keys = m_globalBuckets.keys();
        std::sort(keys.begin(), keys.end());
        for (qint64 b : keys) {
            const double t = static_cast<double>(b) - (m_t0 > 0 ? qFloor(m_t0) : 0);
            const double load = (m_globalBuckets.value(b) * bitsPerFrame / m_bitrate) * 100.0;
            m_loadGraph->addData(t, load);
        }
    } else if (mode == 2) {
        for (auto it = m_idBuckets.constBegin(); it != m_idBuckets.constEnd(); ++it) {
            auto *g = m_idGraphs.value(it.key());
            if (!g)
                continue;
            g->data()->clear();
            QList<qint64> keys = it.value().keys();
            std::sort(keys.begin(), keys.end());
            for (qint64 b : keys) {
                const double t = static_cast<double>(b) - (m_t0 > 0 ? qFloor(m_t0) : 0);
                g->addData(t, it.value().value(b));
            }
        }
    }

    m_plot->rescaleAxes();
    m_plot->replot(QCustomPlot::rpQueuedReplot);

    const double dur = (m_t0 >= 0.0) ? qMax(0.0, m_lastTs - m_t0) : 0.0;
    m_statusLabel->setText(tr("Frames: %1  |  Span: %2 s")
                               .arg(m_totalFrames)
                               .arg(dur, 0, 'f', 1));
}
