#include "dbcsignallistview.h"

#include "core/dbcmanager.h"
#include "core/dbcdata.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QComboBox>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QFile>
#include <QIODevice>

DbcSignalListView::DbcSignalListView(DbcManager *dbc, QWidget *parent)
    : QWidget(parent)
    , m_dbc(dbc)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    auto *bar = new QHBoxLayout;
    m_fileCombo = new QComboBox(this);
    m_fileCombo->addItem(tr("All DBC files"), QString());
    connect(m_fileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DbcSignalListView::onFileFilterChanged);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter message / signal…"));
    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &DbcSignalListView::onFilterChanged);

    auto *exportBtn = new QToolButton(this);
    exportBtn->setText(tr("Export CSV"));
    connect(exportBtn, &QToolButton::clicked, this, &DbcSignalListView::onExportCsv);

    auto *refreshBtn = new QToolButton(this);
    refreshBtn->setText(tr("Refresh"));
    connect(refreshBtn, &QToolButton::clicked, this, &DbcSignalListView::refresh);

    m_countLabel = new QLabel(this);

    bar->addWidget(m_fileCombo, 1);
    bar->addWidget(m_filterEdit, 2);
    bar->addWidget(refreshBtn);
    bar->addWidget(exportBtn);
    bar->addWidget(m_countLabel);
    root->addLayout(bar);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        tr("DBC"), tr("Msg ID"), tr("Message"), tr("Signal"),
        tr("Start"), tr("Length"), tr("Factor"), tr("Unit")
    });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    root->addWidget(m_table, 1);

    if (m_dbc) {
        connect(m_dbc, &DbcManager::dbcLoaded, this, &DbcSignalListView::refresh);
        connect(m_dbc, &DbcManager::dbcUnloaded, this, &DbcSignalListView::refresh);
    }
    refresh();
}

void DbcSignalListView::refresh()
{
    const QString cur = m_fileCombo->currentData().toString();
    m_fileCombo->blockSignals(true);
    m_fileCombo->clear();
    m_fileCombo->addItem(tr("All DBC files"), QString());
    if (m_dbc) {
        for (const DbcFile &f : m_dbc->files())
            m_fileCombo->addItem(f.fileName, f.fileName);
    }
    int idx = m_fileCombo->findData(cur);
    m_fileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_fileCombo->blockSignals(false);
    rebuildTable();
}

void DbcSignalListView::onFilterChanged(const QString &)
{
    rebuildTable();
}

void DbcSignalListView::onFileFilterChanged(int)
{
    rebuildTable();
}

void DbcSignalListView::rebuildTable()
{
    m_table->setRowCount(0);
    if (!m_dbc)
        return;

    const QString fileFilter = m_fileCombo->currentData().toString();
    const QString textFilter = m_filterEdit->text().trimmed().toLower();

    int rows = 0;
    for (const DbcFile &file : m_dbc->files()) {
        if (!fileFilter.isEmpty() && file.fileName != fileFilter)
            continue;
        for (const DbcMessage &msg : file.messages) {
            for (const DbcSignal &sig : msg.signalList) {
                if (!textFilter.isEmpty()) {
                    const QString hay = (msg.name + QLatin1Char(' ') + sig.name).toLower();
                    if (!hay.contains(textFilter))
                        continue;
                }
                const int r = m_table->rowCount();
                m_table->insertRow(r);
                m_table->setItem(r, 0, new QTableWidgetItem(file.fileName));
                m_table->setItem(r, 1, new QTableWidgetItem(
                    QStringLiteral("0x%1").arg(msg.id, 0, 16).toUpper()));
                m_table->setItem(r, 2, new QTableWidgetItem(msg.name));
                m_table->setItem(r, 3, new QTableWidgetItem(sig.name));
                m_table->setItem(r, 4, new QTableWidgetItem(QString::number(sig.startBit)));
                m_table->setItem(r, 5, new QTableWidgetItem(QString::number(sig.bitLength)));
                m_table->setItem(r, 6, new QTableWidgetItem(QString::number(sig.factor)));
                m_table->setItem(r, 7, new QTableWidgetItem(sig.unit));
                ++rows;
            }
        }
    }
    m_countLabel->setText(tr("%1 signals").arg(rows));
}

void DbcSignalListView::onExportCsv()
{
    if (m_table->rowCount() == 0) {
        QMessageBox::information(this, tr("Export"), tr("No signals to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export signal list"), QStringLiteral("dbc_signals.csv"),
        tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export"), tr("Cannot write file."));
        return;
    }
    QTextStream out(&f);
    out << "DBC,MsgID,Message,Signal,Start,Length,Factor,Unit\n";
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList cols;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            QString v = m_table->item(r, c) ? m_table->item(r, c)->text() : QString();
            if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')))
                v = QLatin1Char('"') + v.replace(QLatin1Char('"'), QStringLiteral("\"\""))
                    + QLatin1Char('"');
            cols << v;
        }
        out << cols.join(QLatin1Char(',')) << '\n';
    }
    m_countLabel->setText(tr("Exported %1 signals").arg(m_table->rowCount()));
}
