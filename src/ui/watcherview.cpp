#include "watcherview.h"
#include "core/dbcmanager.h"
#include "core/busstatistics.h"
#include "core/appconfig.h"
#include "ui/dbcsignalpickerdialog.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"
#include "utils/canutils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QComboBox>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <QColor>
#include <QBrush>
#include <cmath>

namespace {
constexpr int kRateValues[] = {50, 100, 200, 500, 1000, 2000};
}

WatcherView::WatcherView(DbcManager *dbc, BusStatistics *stats, QWidget *parent)
    : QWidget(parent), m_dbc(dbc), m_stats(stats)
{
    m_clock.start();
    m_refreshMs = AppConfig::instance()->getInt(
        QStringLiteral("watcher.refreshMs"), 200);
    bool okRate = false;
    for (int v : kRateValues) {
        if (v == m_refreshMs) {
            okRate = true;
            break;
        }
    }
    if (!okRate)
        m_refreshMs = 200;

    setupUi();
    applyRefreshMs(m_refreshMs);
    connect(&m_refreshTimer, &QTimer::timeout,
            this, &WatcherView::onRefreshTimer);
    m_refreshTimer.start();
}

void WatcherView::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    m_addBtn = new QToolButton(m_toolbar);
    m_addBtn->setIcon(svgIcon(":/icons/plus.svg", iconCol, 16));
    m_addBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_removeBtn = new QToolButton(m_toolbar);
    m_removeBtn->setIcon(svgIcon(":/icons/dash.svg", iconCol, 16));

    m_clearBtn = new QToolButton(m_toolbar);
    m_clearBtn->setIcon(svgIcon(":/icons/close.svg", iconCol, 16));

    m_pauseBtn = new QToolButton(m_toolbar);
    m_pauseBtn->setIcon(svgIcon(":/icons/pause.svg", iconCol, 16));
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_resetBtn = new QToolButton(m_toolbar);
    m_resetBtn->setIcon(svgIcon(":/icons/refresh.svg", iconCol, 16));
    m_resetBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_rateLbl = new QLabel(m_toolbar);
    m_rateLbl->setContentsMargins(8, 0, 4, 0);
    m_rateCombo = new QComboBox(m_toolbar);
    for (int v : kRateValues)
        m_rateCombo->addItem(QString(), v);
    {
        const int idx = m_rateCombo->findData(m_refreshMs);
        m_rateCombo->setCurrentIndex(idx >= 0 ? idx : 2);
    }

    m_sampleLbl = new QLabel(m_toolbar);
    m_sampleLbl->setContentsMargins(8, 0, 4, 0);
    m_sampleCombo = new QComboBox(m_toolbar);
    m_sampleCombo->addItem(QString(), 0);
    m_sampleCombo->addItem(QString(), 1);

    m_recBtn = new QToolButton(m_toolbar);
    m_recBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_stopRecBtn = new QToolButton(m_toolbar);
    m_stopRecBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_exportBtn = new QToolButton(m_toolbar);
    m_exportBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_toolbar->addWidget(m_addBtn);
    m_toolbar->addWidget(m_removeBtn);
    m_toolbar->addWidget(m_clearBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_pauseBtn);
    m_toolbar->addWidget(m_resetBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_rateLbl);
    m_toolbar->addWidget(m_rateCombo);
    m_toolbar->addWidget(m_sampleLbl);
    m_toolbar->addWidget(m_sampleCombo);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_recBtn);
    m_toolbar->addWidget(m_stopRecBtn);
    m_toolbar->addWidget(m_exportBtn);
    lay->addWidget(m_toolbar);

    m_tabs = new QTabWidget(this);
    lay->addWidget(m_tabs, 1);

    // ---- Watch tab: table + history ----
    auto *watchPage = new QWidget(this);
    auto *watchLay = new QVBoxLayout(watchPage);
    watchLay->setContentsMargins(0, 0, 0, 0);
    watchLay->setSpacing(0);

    m_watchSplitter = new QSplitter(Qt::Vertical, watchPage);
    watchLay->addWidget(m_watchSplitter, 1);

    m_watchTable = new QTableWidget(0, ColCount, this);
    m_watchTable->setObjectName(QStringLiteral("ContentTable"));
    m_watchTable->setHorizontalHeaderLabels(QStringList() << QString() << QString()
        << QString() << QString() << QString() << QString() << QString()
        << QString() << QString() << QString());
    m_watchTable->setAlternatingRowColors(true);
    m_watchTable->setShowGrid(false);
    m_watchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_watchTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_watchTable->verticalHeader()->setVisible(false);
    m_watchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_watchTable->horizontalHeader()->setHighlightSections(false);
    m_watchTable->horizontalHeader()->setDefaultAlignment(
        Qt::AlignLeft | Qt::AlignVCenter);
    m_watchTable->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_watchTable->horizontalHeader()->setSectionResizeMode(ColMessage, QHeaderView::Stretch);
    for (int i = 0; i < ColCount; ++i) {
        if (i != ColName && i != ColMessage)
            m_watchTable->horizontalHeader()->setSectionResizeMode(
                i, QHeaderView::ResizeToContents);
    }
    m_watchSplitter->addWidget(m_watchTable);

    auto *histHost = new QWidget(this);
    auto *histLay = new QVBoxLayout(histHost);
    histLay->setContentsMargins(4, 4, 4, 4);
    histLay->setSpacing(4);
    m_historyHint = new QLabel(histHost);
    m_historyHint->setObjectName(QStringLiteral("DimLabel"));
    histLay->addWidget(m_historyHint);
    m_historyTable = new QTableWidget(0, 3, histHost);
    m_historyTable->setObjectName(QStringLiteral("ContentTable"));
    m_historyTable->setHorizontalHeaderLabels(QStringList() << QString() << QString() << QString());
    m_historyTable->setAlternatingRowColors(true);
    m_historyTable->setShowGrid(false);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->horizontalHeader()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_historyTable->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    m_historyTable->horizontalHeader()->setSectionResizeMode(
        2, QHeaderView::ResizeToContents);
    histLay->addWidget(m_historyTable, 1);
    m_watchSplitter->addWidget(histHost);
    m_watchSplitter->setStretchFactor(0, 3);
    m_watchSplitter->setStretchFactor(1, 1);

    m_tabs->addTab(watchPage, QString());

    // ---- Bus statistics (unchanged layout) ----
    auto *statPage = new QWidget(this);
    auto *statLay = new QVBoxLayout(statPage);
    statLay->setContentsMargins(8, 8, 8, 8);
    statLay->setSpacing(8);

    m_summaryLabel = new QLabel(statPage);
    m_summaryLabel->setObjectName(QStringLiteral("DimLabel"));
    m_summaryLabel->setAlignment(Qt::AlignCenter);
    statLay->addWidget(m_summaryLabel);

    m_idTable = new QTableWidget(0, 9, statPage);
    m_idTable->setObjectName(QStringLiteral("ContentTable"));
    m_idTable->setHorizontalHeaderLabels(QStringList()
        << QString() << QString() << QString() << QString() << QString()
        << QString() << QString() << QString() << QString());
    m_idTable->setAlternatingRowColors(true);
    m_idTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_idTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_idTable->verticalHeader()->setVisible(false);
    m_idTable->setShowGrid(false);
    m_idTable->horizontalHeader()->setHighlightSections(false);
    m_idTable->horizontalHeader()->setDefaultAlignment(
        Qt::AlignLeft | Qt::AlignVCenter);
    m_idTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int i = 1; i < 9; ++i)
        m_idTable->horizontalHeader()->setSectionResizeMode(
            i, QHeaderView::ResizeToContents);
    statLay->addWidget(m_idTable, 3);

    m_errorTable = new QTableWidget(1, 7, statPage);
    m_errorTable->setObjectName(QStringLiteral("ContentTable"));
    m_errorTable->setHorizontalHeaderLabels(QStringList()
        << QString() << QString() << QString() << QString()
        << QString() << QString() << QString());
    m_errorTable->verticalHeader()->setVisible(false);
    m_errorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_errorTable->setShowGrid(false);
    m_errorTable->setMaximumHeight(86);
    for (int c = 0; c < 7; ++c) {
        auto *it = new QTableWidgetItem(QStringLiteral("0"));
        it->setTextAlignment(Qt::AlignCenter);
        m_errorTable->setItem(0, c, it);
    }
    statLay->addWidget(m_errorTable);
    m_tabs->addTab(statPage, QString());

    connect(m_addBtn, &QToolButton::clicked, this, &WatcherView::onAddVariables);
    connect(m_removeBtn, &QToolButton::clicked, this, &WatcherView::onRemoveSelected);
    connect(m_clearBtn, &QToolButton::clicked, this, &WatcherView::onClearVariables);
    connect(m_resetBtn, &QToolButton::clicked, this, &WatcherView::onResetStats);
    connect(m_pauseBtn, &QToolButton::toggled, this, [this](bool paused) {
        m_paused = paused;
        if (!paused)
            onRefreshTimer();
    });
    connect(m_rateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WatcherView::onRefreshRateChanged);
    connect(m_sampleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WatcherView::onSampleModeChanged);
    connect(m_recBtn, &QToolButton::clicked, this, &WatcherView::onRecordSelected);
    connect(m_stopRecBtn, &QToolButton::clicked, this, &WatcherView::onStopRecordSelected);
    connect(m_exportBtn, &QToolButton::clicked, this, &WatcherView::onExportCsv);
    connect(m_watchTable->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &WatcherView::onWatchSelectionChanged);
    connect(m_watchTable, &QTableWidget::cellChanged,
            this, &WatcherView::onRecCellChanged);

    retranslateUi();
}

void WatcherView::retranslateUi()
{
    m_addBtn->setText(tr("Add…"));
    m_addBtn->setToolTip(tr("Add DBC signals to the watch list"));
    m_removeBtn->setToolTip(tr("Remove selected watches"));
    m_clearBtn->setToolTip(tr("Clear all watches"));
    m_pauseBtn->setText(tr("Freeze"));
    m_pauseBtn->setToolTip(
        tr("Freeze the watch table (like debugger freeze). Frames still update in the background."));
    m_resetBtn->setText(tr("Reset"));
    m_resetBtn->setToolTip(tr("Clear bus statistics and Min/Max for watched signals"));
    m_rateLbl->setText(tr("Update"));
    m_rateCombo->setToolTip(tr("UI refresh interval (debugger watch update rate)"));
    const int rateIdx = m_rateCombo->currentIndex();
    for (int i = 0; i < m_rateCombo->count(); ++i)
        m_rateCombo->setItemText(i, tr("%1 ms").arg(m_rateCombo->itemData(i).toInt()));
    if (rateIdx >= 0)
        m_rateCombo->setCurrentIndex(rateIdx);
    m_sampleLbl->setText(tr("Sample"));
    const int sampleIdx = m_sampleCombo->currentIndex();
    m_sampleCombo->setItemText(0, tr("Every update"));
    m_sampleCombo->setItemText(1, tr("On change"));
    if (sampleIdx >= 0)
        m_sampleCombo->setCurrentIndex(sampleIdx);
    m_sampleCombo->setToolTip(
        tr("When recording: sample every UI tick, or only when the physical value changes"));
    m_recBtn->setText(tr("Record"));
    m_recBtn->setToolTip(tr("Start recording history for selected rows"));
    m_stopRecBtn->setText(tr("Stop Rec"));
    m_stopRecBtn->setToolTip(tr("Stop recording on selected rows"));
    m_exportBtn->setText(tr("Export CSV"));
    m_exportBtn->setToolTip(tr("Export recorded samples (selected rows, or all with history)"));

    m_watchTable->setHorizontalHeaderLabels({
        tr("Name"), tr("Value"), tr("Symbolic"), tr("Raw"),
        tr("Unit"), tr("Message"), tr("Age"), tr("Min"), tr("Max"), tr("Rec")
    });
    m_historyHint->setText(
        tr("Select a watch and enable Rec to capture samples (ring buffer)."));
    m_historyTable->setHorizontalHeaderLabels({
        tr("Time (ms)"), tr("Value"), tr("Raw")
    });
    m_idTable->setHorizontalHeaderLabels({
        tr("ID"), tr("Frames"), tr("Freq Hz"),
        tr("Avg period ms"), tr("Min period"),
        tr("Max period"), tr("Jitter σ ms"),
        tr("Bytes"), tr("Periodic")
    });
    m_errorTable->setHorizontalHeaderLabels({
        QStringLiteral("Stuff"), QStringLiteral("Form"), QStringLiteral("ACK"),
        QStringLiteral("Bit0"), QStringLiteral("Bit1"), QStringLiteral("CRC"),
        tr("Total")
    });
    if (m_tabs) {
        m_tabs->setTabText(0, tr("Watch"));
        m_tabs->setTabText(1, tr("Bus Statistics"));
    }
}

void WatcherView::applyRefreshMs(int ms)
{
    m_refreshMs = ms;
    m_refreshTimer.setInterval(ms);
    AppConfig::instance()->set(QStringLiteral("watcher.refreshMs"), ms);
}

void WatcherView::onRefreshRateChanged(int index)
{
    if (index < 0 || !m_rateCombo)
        return;
    applyRefreshMs(m_rateCombo->itemData(index).toInt());
}

void WatcherView::onSampleModeChanged(int index)
{
    m_sampleOnChange = (index == 1);
}

// ============================================================
//  Variables
// ============================================================

void WatcherView::onAddVariables()
{
    DbcSignalPickerDialog dlg(m_dbc, tr("Add watch variables"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto picked = dlg.pickedSignals();
    int added = 0;
    m_recGuard = true;
    for (const auto &p : picked) {
        bool exists = false;
        for (const auto &e : m_entries) {
            if (e.canId == p.canId && e.name == p.signal.name) {
                exists = true;
                break;
            }
        }
        if (exists)
            continue;

        WatchEntry entry;
        entry.name = p.signal.name;
        entry.messageName = p.messageName;
        entry.canId = p.canId;
        entry.extended = p.extended;
        entry.sig = p.signal;
        m_entries.append(entry);

        const int row = m_watchTable->rowCount();
        m_watchTable->insertRow(row);
        for (int c = 0; c < ColCount; ++c) {
            if (c == ColRec) {
                auto *it = new QTableWidgetItem;
                it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                             | Qt::ItemIsSelectable);
                it->setCheckState(Qt::Unchecked);
                it->setTextAlignment(Qt::AlignCenter);
                m_watchTable->setItem(row, c, it);
            } else {
                auto *it = new QTableWidgetItem(
                    c == ColName ? entry.name : QStringLiteral("—"));
                if (c == ColValue || c == ColRaw || c == ColMin || c == ColMax
                    || c == ColAge)
                    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_watchTable->setItem(row, c, it);
            }
        }
        ++added;
    }
    m_recGuard = false;
    if (added > 0)
        refreshWatchTable();
}

void WatcherView::onRemoveSelected()
{
    const auto rows = m_watchTable->selectionModel()->selectedRows();
    for (int i = rows.size() - 1; i >= 0; --i) {
        const int r = rows.at(i).row();
        if (r < 0 || r >= m_entries.size())
            continue;
        m_entries.removeAt(r);
        m_watchTable->removeRow(r);
    }
    refreshHistoryPane();
}

void WatcherView::onClearVariables()
{
    m_entries.clear();
    m_watchTable->setRowCount(0);
    m_latestFrames.clear();
    m_frameWallMs.clear();
    refreshHistoryPane();
}

void WatcherView::loadWatchEntries(const QVector<WatchEntry> &entries)
{
    m_entries.clear();
    m_watchTable->setRowCount(0);
    m_latestFrames.clear();
    m_frameWallMs.clear();
    m_recGuard = true;

    for (const auto &src : entries) {
        WatchEntry entry = src;
        entry.hasValue = false;
        entry.minValue = 0.0;
        entry.maxValue = 0.0;
        entry.currentValue = 0.0;
        entry.lastPhys = 0.0;
        entry.changedUntilMs = 0;
        entry.lastSampleMs = 0;
        entry.history.clear();
        if (m_dbc) {
            const DbcMessage *msg = m_dbc->findMessage(entry.canId);
            if (msg) {
                if (const DbcSignal *ds = msg->findSignal(entry.name))
                    entry.sig = *ds;
                if (entry.messageName.isEmpty())
                    entry.messageName = msg->name;
            }
        }
        m_entries.append(entry);
        const int row = m_watchTable->rowCount();
        m_watchTable->insertRow(row);
        for (int c = 0; c < ColCount; ++c) {
            if (c == ColRec) {
                auto *it = new QTableWidgetItem;
                it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
                             | Qt::ItemIsSelectable);
                it->setCheckState(entry.recording ? Qt::Checked : Qt::Unchecked);
                it->setTextAlignment(Qt::AlignCenter);
                m_watchTable->setItem(row, c, it);
            } else {
                auto *it = new QTableWidgetItem(
                    c == ColName ? entry.name : QStringLiteral("—"));
                m_watchTable->setItem(row, c, it);
            }
        }
    }
    m_recGuard = false;
    refreshWatchTable();
    refreshHistoryPane();
}

void WatcherView::onResetStats()
{
    if (m_stats)
        m_stats->clear();
    for (auto &e : m_entries) {
        e.hasValue = false;
        e.minValue = 0.0;
        e.maxValue = 0.0;
        e.changedUntilMs = 0;
    }
    m_latestFrames.clear();
    m_frameWallMs.clear();
    refreshWatchTable();
    refreshStatistics();
}

void WatcherView::onRecordSelected()
{
    m_recGuard = true;
    const auto rows = m_watchTable->selectionModel()->selectedRows();
    for (const auto &idx : rows) {
        const int r = idx.row();
        if (r < 0 || r >= m_entries.size())
            continue;
        m_entries[r].recording = true;
        if (auto *it = m_watchTable->item(r, ColRec))
            it->setCheckState(Qt::Checked);
    }
    m_recGuard = false;
    refreshHistoryPane();
}

void WatcherView::onStopRecordSelected()
{
    m_recGuard = true;
    const auto rows = m_watchTable->selectionModel()->selectedRows();
    for (const auto &idx : rows) {
        const int r = idx.row();
        if (r < 0 || r >= m_entries.size())
            continue;
        m_entries[r].recording = false;
        if (auto *it = m_watchTable->item(r, ColRec))
            it->setCheckState(Qt::Unchecked);
    }
    m_recGuard = false;
    refreshHistoryPane();
}

void WatcherView::onRecCellChanged(int row, int column)
{
    if (m_recGuard || column != ColRec)
        return;
    if (row < 0 || row >= m_entries.size())
        return;
    auto *it = m_watchTable->item(row, ColRec);
    if (!it)
        return;
    m_entries[row].recording = (it->checkState() == Qt::Checked);
    refreshHistoryPane();
}

void WatcherView::onExportCsv()
{
    QVector<int> rows;
    const auto sel = m_watchTable->selectionModel()->selectedRows();
    for (const auto &idx : sel)
        rows.append(idx.row());
    if (rows.isEmpty()) {
        for (int i = 0; i < m_entries.size(); ++i) {
            if (!m_entries[i].history.isEmpty())
                rows.append(i);
        }
    }
    if (rows.isEmpty()) {
        QMessageBox::information(
            this, tr("Export CSV"),
            tr("No recorded samples. Enable Rec on one or more watches first."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export watch history"),
        QStringLiteral("watcher_history.csv"),
        tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export CSV"),
                             tr("Cannot write %1").arg(path));
        return;
    }
    QTextStream out(&f);
    out << "time_ms,signal,can_id,phys,raw\n";
    for (int r : rows) {
        if (r < 0 || r >= m_entries.size())
            continue;
        const auto &e = m_entries[r];
        for (const auto &s : e.history) {
            out << QString::number(s.tMs, 'f', 3) << ','
                << e.name << ','
                << QStringLiteral("0x") << QString::number(e.canId, 16).toUpper()
                << ','
                << QString::number(s.phys, 'g', 12) << ','
                << QStringLiteral("0x") << QString::number(s.raw, 16).toUpper()
                << '\n';
        }
    }
    f.close();
    QMessageBox::information(this, tr("Export CSV"),
                             tr("Wrote %1").arg(path));
}

void WatcherView::onWatchSelectionChanged()
{
    refreshHistoryPane();
}

int WatcherView::selectedWatchRow() const
{
    const auto rows = m_watchTable->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return -1;
    return rows.first().row();
}

// ============================================================
//  Data path
// ============================================================

void WatcherView::onFrame(const CanFrame &frame)
{
    const quint32 rawId = frame.id & 0x1FFFFFFF;
    m_latestFrames[rawId] = frame;
    m_frameWallMs[rawId] = m_clock.elapsed();
    maybeSampleFromFrame(rawId, frame);
}

void WatcherView::maybeSampleFromFrame(quint32 canId, const CanFrame &frame)
{
    const qint64 now = m_clock.elapsed();
    for (auto &e : m_entries) {
        if (!e.recording || e.canId != canId)
            continue;
        const double phys = e.sig.decode(frame.data);
        const double factor = (e.sig.factor != 0.0) ? e.sig.factor : 1.0;
        const quint64 rawVal =
            static_cast<quint64>((phys - e.sig.offset) / factor + 0.5);

        if (m_sampleOnChange && e.hasValue
            && std::fabs(phys - e.lastPhys) < 1e-12)
            continue;
        if (!m_sampleOnChange && e.lastSampleMs > 0
            && (now - e.lastSampleMs) < m_refreshMs)
            continue;

        const double tMs = (frame.timestamp > 0.0)
            ? (frame.timestamp * 1000.0)
            : static_cast<double>(now);
        pushSample(e, phys, rawVal, tMs);
        e.lastSampleMs = now;
    }
}

void WatcherView::pushSample(WatchEntry &e, double phys, quint64 raw, double tMs)
{
    Sample s;
    s.tMs = tMs;
    s.phys = phys;
    s.raw = raw;
    if (e.history.size() >= kRingCapacity)
        e.history.removeFirst();
    e.history.append(s);
}

void WatcherView::clearData()
{
    for (auto &e : m_entries) {
        e.hasValue = false;
        e.minValue = 0.0;
        e.maxValue = 0.0;
        e.changedUntilMs = 0;
        e.history.clear();
        e.lastSampleMs = 0;
    }
    m_latestFrames.clear();
    m_frameWallMs.clear();
    refreshWatchTable();
    refreshHistoryPane();
}

void WatcherView::onRefreshTimer()
{
    if (m_paused || !isVisible())
        return;
    refreshWatchTable();
    refreshStatistics();
    refreshHistoryPane();
}

void WatcherView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
    onRefreshTimer();
}

void WatcherView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_refreshTimer.stop();
}

QString WatcherView::formatAge(qint64 lastWallMs) const
{
    if (lastWallMs <= 0)
        return QStringLiteral("—");
    const qint64 age = m_clock.elapsed() - lastWallMs;
    if (age < 1000)
        return tr("%1 ms").arg(age);
    if (age < 60000)
        return tr("%1 s").arg(QString::number(age / 1000.0, 'f', 1));
    return tr("%1 min").arg(age / 60000);
}

QString WatcherView::symbolicFor(const WatchEntry &e, quint64 raw) const
{
    const QString desc = e.sig.lookupValueDesc(static_cast<int>(raw));
    return desc.isEmpty() ? QStringLiteral("—") : desc;
}

void WatcherView::refreshWatchTable()
{
    const QColor accent(ThemeManager::instance()->currentTheme().accent);
    const qint64 now = m_clock.elapsed();

    for (int i = 0; i < m_entries.size(); ++i) {
        auto &entry = m_entries[i];
        auto it = m_latestFrames.constFind(entry.canId);
        if (it == m_latestFrames.constEnd()) {
            if (i < m_watchTable->rowCount()) {
                if (auto *age = m_watchTable->item(i, ColAge))
                    age->setText(formatAge(m_frameWallMs.value(entry.canId, 0)));
            }
            continue;
        }

        const CanFrame &frame = it.value();
        const double physValue = entry.sig.decode(frame.data);
        const double factor = (entry.sig.factor != 0.0) ? entry.sig.factor : 1.0;
        const quint64 rawVal =
            static_cast<quint64>((physValue - entry.sig.offset) / factor + 0.5);

        const bool changed = entry.hasValue
            && std::fabs(physValue - entry.currentValue) > 1e-12;
        if (changed)
            entry.changedUntilMs = now + kHighlightMs;

        entry.lastPhys = entry.hasValue ? entry.currentValue : physValue;
        entry.currentValue = physValue;
        if (!entry.hasValue) {
            entry.minValue = physValue;
            entry.maxValue = physValue;
            entry.hasValue = true;
        } else {
            if (physValue < entry.minValue) entry.minValue = physValue;
            if (physValue > entry.maxValue) entry.maxValue = physValue;
        }

        if (i >= m_watchTable->rowCount())
            continue;

        auto setText = [this, i](int col, const QString &t) {
            if (auto *it = m_watchTable->item(i, col))
                it->setText(t);
        };
        setText(ColName, entry.name);
        setText(ColValue, QString::number(physValue, 'f', 3));
        setText(ColSymbolic, symbolicFor(entry, rawVal));
        setText(ColRaw, QStringLiteral("0x") + QString::number(rawVal, 16).toUpper());
        setText(ColUnit, entry.sig.unit);
        setText(ColMessage,
                QStringLiteral("%1 %2")
                    .arg(CanUtils::formatId(entry.canId, entry.extended),
                         entry.messageName));
        setText(ColAge, formatAge(m_frameWallMs.value(entry.canId, 0)));
        setText(ColMin, QString::number(entry.minValue, 'f', 3));
        setText(ColMax, QString::number(entry.maxValue, 'f', 3));

        if (auto *valItem = m_watchTable->item(i, ColValue)) {
            if (entry.changedUntilMs > now)
                valItem->setForeground(accent);
            else
                valItem->setForeground(QBrush());
        }

        m_recGuard = true;
        if (auto *rec = m_watchTable->item(i, ColRec))
            rec->setCheckState(entry.recording ? Qt::Checked : Qt::Unchecked);
        m_recGuard = false;
    }
}

void WatcherView::refreshHistoryPane()
{
    const int row = selectedWatchRow();
    m_historyTable->setRowCount(0);
    if (row < 0 || row >= m_entries.size()) {
        m_historyHint->setText(
            tr("Select a watch and enable Rec to capture samples (ring buffer)."));
        return;
    }
    const auto &e = m_entries[row];
    if (e.history.isEmpty()) {
        m_historyHint->setText(
            e.recording
                ? tr("Recording %1 — waiting for samples…").arg(e.name)
                : tr("%1 — enable Rec to start the ring buffer.").arg(e.name));
        return;
    }
    m_historyHint->setText(
        tr("%1 — %2 samples (cap %3)%4")
            .arg(e.name)
            .arg(e.history.size())
            .arg(kRingCapacity)
            .arg(e.recording ? tr(" · recording") : QString()));

    // Show newest last (scroll natural); limit display to last 500 for UI
    const int n = e.history.size();
    const int start = qMax(0, n - 500);
    m_historyTable->setRowCount(n - start);
    for (int i = start; i < n; ++i) {
        const auto &s = e.history[i];
        const int r = i - start;
        m_historyTable->setItem(
            r, 0, new QTableWidgetItem(QString::number(s.tMs, 'f', 3)));
        m_historyTable->setItem(
            r, 1, new QTableWidgetItem(QString::number(s.phys, 'f', 3)));
        m_historyTable->setItem(
            r, 2, new QTableWidgetItem(
                      QStringLiteral("0x")
                      + QString::number(s.raw, 16).toUpper()));
    }
    m_historyTable->scrollToBottom();
}

void WatcherView::refreshStatistics()
{
    if (!m_stats)
        return;

    const auto summary = m_stats->lastSummary();
    quint64 errs[BusStatistics::ErrCount] = {};
    m_stats->lastErrorCounts(errs);

    m_summaryLabel->setText(QStringLiteral(
        "<table cellspacing='14' cellpadding='0'><tr>"
        "<td align='center'><b style='font-size:14px'>%1</b><br>%6</td>"
        "<td align='center'><b style='font-size:14px'>%2</b><br>%7</td>"
        "<td align='center'><b style='font-size:14px'>%3%</b><br>%8</td>"
        "<td align='center'><b style='font-size:14px'>%4 s</b><br>%9</td>"
        "<td align='center'><b style='font-size:14px'>%5</b><br>%10</td>"
        "</tr></table>")
        .arg(summary.totalFrames)
        .arg(summary.uniqueIds)
        .arg(QString::number(summary.busLoadPercent, 'f', 2))
        .arg(QString::number(summary.duration, 'f', 1))
        .arg(summary.errorFrames)
        .arg(tr("Frames"))
        .arg(tr("IDs"))
        .arg(tr("Bus load"))
        .arg(tr("Duration"))
        .arg(tr("Errors")));

    const auto &idStats = m_stats->lastIdStats();
    m_idTable->setRowCount(idStats.size());
    for (int r = 0; r < idStats.size(); ++r) {
        const auto &s = idStats.at(r);
        const QString vals[9] = {
            CanUtils::formatId(s.canId, s.canId > 0x7FF),
            QString::number(s.frameCount),
            QString::number(s.frequency, 'f', 1),
            s.isPeriodic ? QString::number(s.avgPeriod, 'f', 2) : QStringLiteral("—"),
            s.isPeriodic ? QString::number(s.minPeriod, 'f', 2) : QStringLiteral("—"),
            s.isPeriodic ? QString::number(s.maxPeriod, 'f', 2) : QStringLiteral("—"),
            s.isPeriodic ? QString::number(s.jitter, 'f', 2) : QStringLiteral("—"),
            QString::number(s.totalBytes),
            s.isPeriodic ? tr("Periodic") : tr("Event"),
        };
        for (int c = 0; c < 9; ++c) {
            auto *it = m_idTable->item(r, c);
            if (!it) {
                it = new QTableWidgetItem(vals[c]);
                m_idTable->setItem(r, c, it);
            } else {
                it->setText(vals[c]);
            }
        }
    }

    quint64 total = 0;
    for (int i = 0; i < BusStatistics::ErrCount; ++i)
        total += errs[i];
    const quint64 shown[7] = {
        errs[BusStatistics::ErrStuff], errs[BusStatistics::ErrForm],
        errs[BusStatistics::ErrAck], errs[BusStatistics::ErrBit0],
        errs[BusStatistics::ErrBit1], errs[BusStatistics::ErrCrc], total,
    };
    for (int c = 0; c < 7; ++c)
        m_errorTable->item(0, c)->setText(QString::number(shown[c]));
}
