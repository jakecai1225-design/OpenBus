#include "watcherview.h"
#include "core/dbcmanager.h"
#include "core/busstatistics.h"
#include "ui/dbcsignalpickerdialog.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"
#include "utils/canutils.h"

#include <QVBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>

// ============================================================
//  Watcher 观测窗口（doc/Watcher方案.md 方案 A：壳侧页面）
//  数据流：onFrame → 最新帧缓存；统计 = BusStatistics 快照轮询
// ============================================================

WatcherView::WatcherView(DbcManager *dbc, BusStatistics *stats, QWidget *parent)
    : QWidget(parent), m_dbc(dbc), m_stats(stats)
{
    setupUi();
    m_refreshTimer.setInterval(500);
    connect(&m_refreshTimer, &QTimer::timeout,
            this, &WatcherView::onRefreshTimer);
    m_refreshTimer.start();
}

void WatcherView::setupUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // ---- 工具栏 ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    auto *addBtn = new QToolButton(m_toolbar);
    addBtn->setIcon(svgIcon(":/icons/plus.svg", iconCol, 16));
    addBtn->setText(QStringLiteral("添加变量"));
    addBtn->setToolTip(
        QStringLiteral("从已加载的 DBC 数据库中搜索/多选信号加入观测列表"));
    addBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *removeBtn = new QToolButton(m_toolbar);
    removeBtn->setIcon(svgIcon(":/icons/dash.svg", iconCol, 16));
    removeBtn->setToolTip(QStringLiteral("移除选中的观测变量"));
    auto *clearBtn = new QToolButton(m_toolbar);
    clearBtn->setIcon(svgIcon(":/icons/close.svg", iconCol, 16));
    clearBtn->setToolTip(QStringLiteral("清空全部观测变量"));
    m_pauseBtn = new QToolButton(m_toolbar);
    m_pauseBtn->setIcon(svgIcon(":/icons/pause.svg", iconCol, 16));
    m_pauseBtn->setText(QStringLiteral("暂停刷新"));
    m_pauseBtn->setToolTip(
        QStringLiteral("冻结当前显示（对应调试器 Watch 的 freeze），数据仍在后台累计"));
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *resetBtn = new QToolButton(m_toolbar);
    resetBtn->setIcon(svgIcon(":/icons/refresh.svg", iconCol, 16));
    resetBtn->setText(QStringLiteral("清零统计"));
    resetBtn->setToolTip(
        QStringLiteral("总线统计引擎与变量 Min/Max 全部归零，重新累计"));
    resetBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_toolbar->addWidget(addBtn);
    m_toolbar->addWidget(removeBtn);
    m_toolbar->addWidget(clearBtn);
    m_toolbar->addSeparator();
    m_toolbar->addWidget(m_pauseBtn);
    m_toolbar->addWidget(resetBtn);
    lay->addWidget(m_toolbar);

    // ---- 两个标签页 ----
    auto *tabs = new QTabWidget(this);
    lay->addWidget(tabs, 1);

    // 页 1：变量观测
    m_watchTable = new QTableWidget(0, 7, this);
    m_watchTable->setHorizontalHeaderLabels({
        QStringLiteral("变量"), QStringLiteral("当前值"), QStringLiteral("原始值"),
        QStringLiteral("最小"), QStringLiteral("最大"), QStringLiteral("单位"),
        QStringLiteral("报文")
    });
    m_watchTable->setAlternatingRowColors(true);
    m_watchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_watchTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_watchTable->verticalHeader()->setVisible(false);
    m_watchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_watchTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 7; ++i)
        m_watchTable->horizontalHeader()->setSectionResizeMode(
            i, QHeaderView::ResizeToContents);
    tabs->addTab(m_watchTable, QStringLiteral("变量观测"));

    // 页 2：总线统计
    auto *statPage = new QWidget(this);
    auto *statLay = new QVBoxLayout(statPage);
    statLay->setContentsMargins(6, 6, 6, 6);
    statLay->setSpacing(6);

    m_summaryLabel = new QLabel(statPage);
    m_summaryLabel->setAlignment(Qt::AlignCenter);
    m_summaryLabel->setStyleSheet(
        QStringLiteral("color:%1;padding:4px;").arg(iconCol));
    statLay->addWidget(m_summaryLabel);

    m_idTable = new QTableWidget(0, 9, statPage);
    m_idTable->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("帧数"), QStringLiteral("频率 Hz"),
        QStringLiteral("平均周期 ms"), QStringLiteral("最小周期"),
        QStringLiteral("最大周期"), QStringLiteral("抖动 σ ms"),
        QStringLiteral("字节"), QStringLiteral("周期性")
    });
    m_idTable->setAlternatingRowColors(true);
    m_idTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_idTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_idTable->verticalHeader()->setVisible(false);
    m_idTable->setShowGrid(true);
    m_idTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int i = 1; i < 9; ++i)
        m_idTable->horizontalHeader()->setSectionResizeMode(
            i, QHeaderView::ResizeToContents);
    statLay->addWidget(m_idTable, 3);

    m_errorTable = new QTableWidget(1, 7, statPage);
    m_errorTable->setHorizontalHeaderLabels({
        QStringLiteral("Stuff"), QStringLiteral("Form"), QStringLiteral("ACK"),
        QStringLiteral("Bit0"), QStringLiteral("Bit1"), QStringLiteral("CRC"),
        QStringLiteral("总计")
    });
    m_errorTable->verticalHeader()->setVisible(false);
    m_errorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_errorTable->setMaximumHeight(86);
    for (int c = 0; c < 7; ++c) {
        auto *it = new QTableWidgetItem(QStringLiteral("0"));
        it->setTextAlignment(Qt::AlignCenter);
        m_errorTable->setItem(0, c, it);
    }
    statLay->addWidget(m_errorTable);

    tabs->addTab(statPage, QStringLiteral("总线统计"));

    // ---- 信号连接 ----
    connect(addBtn, &QToolButton::clicked, this, &WatcherView::onAddVariables);
    connect(removeBtn, &QToolButton::clicked, this, &WatcherView::onRemoveSelected);
    connect(clearBtn, &QToolButton::clicked, this, &WatcherView::onClearVariables);
    connect(resetBtn, &QToolButton::clicked, this, &WatcherView::onResetStats);
    connect(m_pauseBtn, &QToolButton::toggled, this, [this](bool paused) {
        m_paused = paused;
        if (!paused)
            onRefreshTimer();   // 恢复立即刷一帧
    });
}

// ============================================================
//  变量增删
// ============================================================

void WatcherView::onAddVariables()
{
    DbcSignalPickerDialog dlg(m_dbc, QStringLiteral("添加观测变量"), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto picked = dlg.pickedSignals();
    int added = 0;
    for (const auto &p : picked) {
        // 去重：同 ID + 同信号名已观测则跳过
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
        m_watchTable->setItem(row, 0, new QTableWidgetItem(entry.name));
        for (int c = 1; c < 7; ++c)
            m_watchTable->setItem(row, c, new QTableWidgetItem(QStringLiteral("—")));
        ++added;
    }
    if (added > 0)
        refreshWatchTable();
}

void WatcherView::onRemoveSelected()
{
    // 自底向上删除选中行（避免行号位移）
    const auto rows = m_watchTable->selectionModel()->selectedRows();
    for (int i = rows.size() - 1; i >= 0; --i) {
        const int r = rows.at(i).row();
        if (r < 0 || r >= m_entries.size())
            continue;
        m_entries.removeAt(r);
        m_watchTable->removeRow(r);
    }
}

void WatcherView::onClearVariables()
{
    m_entries.clear();
    m_watchTable->setRowCount(0);
    m_latestFrames.clear();
}

void WatcherView::onResetStats()
{
    if (m_stats)
        m_stats->clear();
    for (auto &e : m_entries) {
        e.hasValue = false;
        e.minValue = 0.0;
        e.maxValue = 0.0;
    }
    m_latestFrames.clear();
    refreshWatchTable();
    refreshStatistics();
}

// ============================================================
//  数据入口 / 刷新
// ============================================================

void WatcherView::onFrame(const CanFrame &frame)
{
    quint32 rawId = frame.id & 0x1FFFFFFF;
    m_latestFrames[rawId] = frame;
}

void WatcherView::clearData()
{
    for (auto &e : m_entries) {
        e.hasValue = false;
        e.minValue = 0.0;
        e.maxValue = 0.0;
    }
    m_latestFrames.clear();
    refreshWatchTable();
}

void WatcherView::onRefreshTimer()
{
    if (m_paused)
        return;
    refreshWatchTable();
    refreshStatistics();
}

void WatcherView::refreshWatchTable()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        auto &entry = m_entries[i];
        auto it = m_latestFrames.constFind(entry.canId);
        if (it == m_latestFrames.constEnd())
            continue;

        const CanFrame &frame = it.value();
        // 懒解码：条目自带 DbcSignal 拷贝（不经 DbcManager 按名回查）
        const double physValue = entry.sig.decode(frame.data);
        const double factor = (entry.sig.factor != 0.0) ? entry.sig.factor : 1.0;
        const quint64 rawVal =
            static_cast<quint64>((physValue - entry.sig.offset) / factor + 0.5);

        entry.currentValue = physValue;
        if (!entry.hasValue) {
            entry.minValue = physValue;
            entry.maxValue = physValue;
            entry.hasValue = true;
        } else {
            if (physValue < entry.minValue) entry.minValue = physValue;
            if (physValue > entry.maxValue) entry.maxValue = physValue;
        }

        if (i < m_watchTable->rowCount()) {
            m_watchTable->item(i, 1)->setText(QString::number(physValue, 'f', 3));
            m_watchTable->item(i, 2)->setText(
                QStringLiteral("0x") + QString::number(rawVal, 16).toUpper());
            m_watchTable->item(i, 3)->setText(QString::number(entry.minValue, 'f', 3));
            m_watchTable->item(i, 4)->setText(QString::number(entry.maxValue, 'f', 3));
            m_watchTable->item(i, 5)->setText(entry.sig.unit);
            m_watchTable->item(i, 6)->setText(
                QStringLiteral("%1 %2")
                    .arg(CanUtils::formatId(entry.canId, entry.extended),
                         entry.messageName));
        }
    }
}

void WatcherView::refreshStatistics()
{
    if (!m_stats)
        return;

    // ---- 摘要卡片（HTML 单标签，五项）----
    const auto summary = m_stats->lastSummary();
    quint64 errs[BusStatistics::ErrCount] = {};
    m_stats->lastErrorCounts(errs);

    m_summaryLabel->setText(QStringLiteral(
        "<table cellspacing='14' cellpadding='0'><tr>"
        "<td align='center'><b style='font-size:14px'>%1</b><br>总帧数</td>"
        "<td align='center'><b style='font-size:14px'>%2</b><br>报文 ID 数</td>"
        "<td align='center'><b style='font-size:14px'>%3%</b><br>总线负载</td>"
        "<td align='center'><b style='font-size:14px'>%4 s</b><br>统计时长</td>"
        "<td align='center'><b style='font-size:14px'>%5</b><br>错误帧</td>"
        "</tr></table>")
        .arg(summary.totalFrames)
        .arg(summary.uniqueIds)
        .arg(QString::number(summary.busLoadPercent, 'f', 2))
        .arg(QString::number(summary.duration, 'f', 1))
        .arg(summary.errorFrames));

    // ---- 逐报文统计表（快照重建；引擎侧已按 canId 升序）----
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
            s.isPeriodic ? QStringLiteral("周期") : QStringLiteral("事件"),
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

    // ---- 错误分类表 ----
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
