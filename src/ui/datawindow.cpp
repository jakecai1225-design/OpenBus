#include "datawindow.h"
#include "signalconfigdialog.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "ui/thememanager.h"
#include "utils/svg_icon.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>

DataWindow::DataWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    m_refreshTimer.setInterval(100);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DataWindow::onRefreshTimer);
    m_refreshTimer.start();
}

void DataWindow::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- 工具栏 ----
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setIconSize(QSize(16, 16));

    const QString iconCol = ThemeManager::instance()->currentTheme().text;
    auto *addBtn = new QToolButton(m_toolbar);
    addBtn->setIcon(svgIcon(":/icons/plus.svg", iconCol, 16));
    addBtn->setToolTip("添加信号");
    auto *removeBtn = new QToolButton(m_toolbar);
    removeBtn->setIcon(svgIcon(":/icons/dash.svg", iconCol, 16));
    removeBtn->setToolTip("删除选中信号");
    auto *clearBtn = new QToolButton(m_toolbar);
    clearBtn->setIcon(svgIcon(":/icons/close.svg", iconCol, 16));
    clearBtn->setToolTip("清空全部");

    m_toolbar->addWidget(addBtn);
    m_toolbar->addWidget(removeBtn);
    m_toolbar->addWidget(clearBtn);
    layout->addWidget(m_toolbar);

    // ---- 表格 ----
    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels({
        "Signal", "Current", "Raw", "Physical", "Min", "Max"
    });
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 6; ++i)
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    // ---- 信号连接 ----
    connect(addBtn, &QToolButton::clicked, this, &DataWindow::onAddSignal);
    connect(removeBtn, &QToolButton::clicked, this, &DataWindow::onRemoveSignal);
    connect(clearBtn, &QToolButton::clicked, this, &DataWindow::onClearAll);
}

void DataWindow::addSignal(const QString &name, quint32 canId, bool extended,
                           double factor, double offset, const QString &unit)
{
    SignalEntry entry;
    entry.name = name;
    entry.canId = canId;
    entry.extended = extended;
    entry.factor = (factor != 0.0) ? factor : 1.0;
    entry.offset = offset;
    entry.unit = unit;
    m_entries.append(entry);

    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(name));
    m_table->setItem(row, 1, new QTableWidgetItem("—"));
    m_table->setItem(row, 2, new QTableWidgetItem("—"));
    m_table->setItem(row, 3, new QTableWidgetItem("—"));
    m_table->setItem(row, 4, new QTableWidgetItem("—"));
    m_table->setItem(row, 5, new QTableWidgetItem("—"));
}

void DataWindow::removeSignal(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    m_table->removeRow(index);
}

void DataWindow::clearSignals()
{
    m_entries.clear();
    m_table->setRowCount(0);
    m_latestFrames.clear();
}

void DataWindow::clearData()
{
    for (auto &e : m_entries) {
        e.hasValue = false;
        e.minValue = 0.0;
        e.maxValue = 0.0;
    }
    m_latestFrames.clear();
    refreshTable();
}

void DataWindow::onFrame(const CanFrame &frame)
{
    quint32 rawId = frame.id & 0x1FFFFFFF;
    m_latestFrames[rawId] = frame;
}

void DataWindow::onRefreshTimer()
{
    if (!isVisible())
        return;
    refreshTable();
}

void DataWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_refreshTimer.isActive())
        m_refreshTimer.start();
    refreshTable();
}

void DataWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_refreshTimer.stop();
}

void DataWindow::refreshTable()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        auto &entry = m_entries[i];
        quint32 rawId = entry.canId;

        auto it = m_latestFrames.constFind(rawId);
        if (it == m_latestFrames.constEnd()) continue;

        const CanFrame &frame = it.value();

        // 解码信号值
        double physValue = 0.0;
        if (m_dbc) {
            const DbcMessage *msg = m_dbc->findMessage(rawId);
            if (msg) {
                const DbcSignal *sig = msg->findSignal(entry.name);
                if (sig) {
                    physValue = sig->decode(frame.data);
                } else {
                    // DBC 中没有此信号定义，跳过
                    continue;
                }
            } else {
                continue;
            }
        } else {
            continue;
        }

        quint64 rawVal = static_cast<quint64>((physValue - entry.offset) / entry.factor + 0.5);

        entry.currentValue = physValue;
        entry.rawValue = rawVal;
        entry.lastTimestamp = frame.timestamp;

        if (!entry.hasValue) {
            entry.minValue = physValue;
            entry.maxValue = physValue;
            entry.hasValue = true;
        } else {
            if (physValue < entry.minValue) entry.minValue = physValue;
            if (physValue > entry.maxValue) entry.maxValue = physValue;
        }

        // 更新表格行
        if (i < m_table->rowCount()) {
            m_table->item(i, 1)->setText(QString::number(physValue, 'f', 3));
            m_table->item(i, 2)->setText("0x" + QString::number(rawVal, 16).toUpper());
            m_table->item(i, 3)->setText(QString("%1 %2").arg(physValue, 0, 'f', 3).arg(entry.unit));
            m_table->item(i, 4)->setText(QString::number(entry.minValue, 'f', 3));
            m_table->item(i, 5)->setText(QString::number(entry.maxValue, 'f', 3));
        }
    }
}

void DataWindow::onAddSignal()
{
    SignalConfigDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        addSignal(dlg.signalName(), dlg.canId(), dlg.isExtended(),
                  1.0, 0.0, QString());
    }
}

void DataWindow::onRemoveSignal()
{
    int row = m_table->currentRow();
    if (row >= 0)
        removeSignal(row);
}

void DataWindow::onClearAll()
{
    clearSignals();
}
