#include "signalsendtab.h"
#include "core/dbcdata.h"
#include "core/dbcmanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QRegularExpression>

// ============================================================
//  Static helpers
// ============================================================

QByteArray SignalSendTab::parseHexData(const QString &text)
{
    QByteArray data;
    QRegularExpression re("[\\s,]+");
    const auto parts = text.split(re, Qt::SkipEmptyParts);
    for (const auto &b : parts) {
        bool ok = false;
        quint8 val = static_cast<quint8>(b.toUInt(&ok, 16));
        if (ok)
            data.append(static_cast<char>(val));
    }
    return data;
}

QString SignalSendTab::formatDataHex(const QByteArray &data)
{
    QString hex;
    for (int i = 0; i < data.size(); ++i) {
        if (i > 0) hex += ' ';
        hex += QString("%1").arg(static_cast<unsigned char>(data[i]), 2, 16, QChar('0')).toUpper();
    }
    return hex;
}

QString SignalSendTab::formatIdHex(quint32 id)
{
    return QString("0x%1").arg(id, 0, 16).toUpper();
}

// ============================================================
//  Constructor
// ============================================================

SignalSendTab::SignalSendTab(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // ---- Toolbar ----
    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->setSpacing(4);
    m_sendAllBtn = new QPushButton("全部发送", this);
    m_stopAllBtn = new QPushButton("全部停止", this);
    m_importDbcBtn = new QPushButton("从DBC导入", this);
    m_clearListBtn = new QPushButton("清空列表", this);
    toolbarLayout->addWidget(m_sendAllBtn);
    toolbarLayout->addWidget(m_stopAllBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_importDbcBtn);
    toolbarLayout->addWidget(m_clearListBtn);
    mainLayout->addLayout(toolbarLayout);

    // ---- Splitter: table (top) + edit area (bottom, taller) ----
    auto *splitter = new QSplitter(Qt::Vertical, this);

    // -- Send list table --
    m_sendTable = new QTableWidget(0, 10, splitter);
    m_sendTable->setHorizontalHeaderLabels(
        {"✓", "#", "ID", "名称", "DLC", "数据(Hex)", "周期(ms)", "次数", "状态", "操作"});
    m_sendTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_sendTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(8, QHeaderView::ResizeToContents);
    m_sendTable->horizontalHeader()->setSectionResizeMode(9, QHeaderView::ResizeToContents);
    m_sendTable->verticalHeader()->setVisible(false);
    m_sendTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    // -- Edit area (QGroupBox) --
    auto *editGroup = new QGroupBox("编辑发送帧", splitter);
    auto *editLayout = new QVBoxLayout(editGroup);
    editLayout->setContentsMargins(8, 8, 8, 8);
    editLayout->setSpacing(6);

    // Row 1: ID, DLC, 数据(Hex)
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(6);
    row1->addWidget(new QLabel("ID:"));
    m_idEdit = new QLineEdit(editGroup);
    m_idEdit->setPlaceholderText("0x123");
    m_idEdit->setMaximumWidth(100);
    row1->addWidget(m_idEdit);

    row1->addWidget(new QLabel("DLC:"));
    m_dlcSpin = new QSpinBox(editGroup);
    m_dlcSpin->setRange(0, 64);
    m_dlcSpin->setValue(8);
    m_dlcSpin->setMaximumWidth(50);
    row1->addWidget(m_dlcSpin);

    row1->addWidget(new QLabel("数据(Hex):"));
    m_dataEdit = new QLineEdit(editGroup);
    m_dataEdit->setPlaceholderText("AA BB CC DD EE FF 00 11");
    row1->addWidget(m_dataEdit, 1);
    editLayout->addLayout(row1);

    // Row 2: 周期, 次数, DBC消息, 按钮
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(6);
    row2->addWidget(new QLabel("周期(ms):"));
    m_periodSpin = new QSpinBox(editGroup);
    m_periodSpin->setRange(0, 100000);
    m_periodSpin->setValue(100);
    m_periodSpin->setMaximumWidth(80);
    row2->addWidget(m_periodSpin);

    row2->addWidget(new QLabel("次数:"));
    m_countSpin = new QSpinBox(editGroup);
    m_countSpin->setRange(0, 999999);
    m_countSpin->setValue(0);
    m_countSpin->setSpecialValueText("∞");
    m_countSpin->setMaximumWidth(60);
    row2->addWidget(m_countSpin);

    row2->addWidget(new QLabel("DBC消息:"));
    m_dbcMsgCombo = new QComboBox(editGroup);
    m_dbcMsgCombo->setMinimumWidth(160);
    m_dbcMsgCombo->setPlaceholderText("(可选)");
    row2->addWidget(m_dbcMsgCombo, 1);

    m_addToListBtn = new QPushButton("添加到列表", editGroup);
    m_sendSingleBtn = new QPushButton("发送单帧", editGroup);
    row2->addWidget(m_addToListBtn);
    row2->addWidget(m_sendSingleBtn);
    editLayout->addLayout(row2);

    // Signal-level editing area
    m_signalHintLabel = new QLabel("输入 CAN ID 后，若 DBC 中有匹配消息，将自动生成信号编辑器", editGroup);
    m_signalHintLabel->setStyleSheet("color: gray; font-style: italic;");
    editLayout->addWidget(m_signalHintLabel);

    m_signalScroll = new QScrollArea(editGroup);
    m_signalScroll->setWidgetResizable(true);
    m_signalScroll->setFrameShape(QFrame::NoFrame);
    auto *signalContainer = new QWidget(m_signalScroll);
    m_signalLayout = new QVBoxLayout(signalContainer);
    m_signalLayout->setContentsMargins(0, 0, 0, 0);
    m_signalLayout->setSpacing(4);
    m_signalLayout->addStretch();
    m_signalScroll->setWidget(signalContainer);
    editLayout->addWidget(m_signalScroll, 1);

    splitter->addWidget(m_sendTable);
    splitter->addWidget(editGroup);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 400});
    mainLayout->addWidget(splitter, 1);

    // ---- Connections ----
    connect(m_sendAllBtn, &QPushButton::clicked, this, &SignalSendTab::onSendAll);
    connect(m_stopAllBtn, &QPushButton::clicked, this, &SignalSendTab::onStopAll);
    connect(m_clearListBtn, &QPushButton::clicked, this, &SignalSendTab::onClearList);
    connect(m_importDbcBtn, &QPushButton::clicked, this, &SignalSendTab::onImportFromDbc);
    connect(m_addToListBtn, &QPushButton::clicked, this, &SignalSendTab::onAddToList);
    connect(m_sendSingleBtn, &QPushButton::clicked, this, &SignalSendTab::onSendSingle);
    connect(m_idEdit, &QLineEdit::editingFinished, this, &SignalSendTab::onIdEditingFinished);
    connect(m_dataEdit, &QLineEdit::editingFinished, this, &SignalSendTab::onDataEditFinished);
    connect(m_dbcMsgCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SignalSendTab::onDbcMsgSelected);
}

// ============================================================
//  DbcManager integration
// ============================================================

void SignalSendTab::setDbcManager(DbcManager *mgr)
{
    m_dbcManager = mgr;
    refreshDbcMessages();
}

void SignalSendTab::refreshDbcMessages()
{
    m_dbcMsgCombo->blockSignals(true);
    m_dbcMsgCombo->clear();
    m_dbcMsgCombo->addItem("(无)", 0);
    if (m_dbcManager) {
        const auto messages = m_dbcManager->allMessages();
        for (const auto *msg : messages) {
            QString label = QString("0x%1 - %2").arg(msg->id, 0, 16).toUpper().arg(msg->name);
            m_dbcMsgCombo->addItem(label, msg->id);
        }
    }
    m_dbcMsgCombo->blockSignals(false);
}

// ============================================================
//  Signal-level editors
// ============================================================

void SignalSendTab::onIdEditingFinished()
{
    QString idText = m_idEdit->text().trimmed();
    if (idText.isEmpty()) {
        m_currentDbcMsg = nullptr;
        rebuildSignalEditors();
        return;
    }

    quint32 id = idText.toUInt(nullptr, 16);
    if (id == 0 && !idText.startsWith("0x", Qt::CaseInsensitive)) {
        // Might be decimal
        id = idText.toUInt();
    }

    const DbcMessage *msg = nullptr;
    if (m_dbcManager)
        msg = m_dbcManager->findMessage(id);

    if (msg != m_currentDbcMsg) {
        m_currentDbcMsg = msg;
        if (msg) {
            m_dlcSpin->setValue(msg->dlc);
        }
        rebuildSignalEditors();
    }
}

void SignalSendTab::onDbcMsgSelected(int index)
{
    if (index <= 0) return;
    quint32 id = m_dbcMsgCombo->itemData(index).toUInt();
    if (id == 0) return;

    m_idEdit->setText(formatIdHex(id));
    onIdEditingFinished();
}

void SignalSendTab::rebuildSignalEditors()
{
    // Clear existing editors
    QLayoutItem *item;
    while ((item = m_signalLayout->takeAt(0)) != nullptr) {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_signalLayout->addStretch();
    m_signalSpinBoxes.clear();

    if (!m_dbcManager || !m_currentDbcMsg) {
        m_signalHintLabel->setText("输入 CAN ID 后，若 DBC 中有匹配消息，将自动生成信号编辑器");
        return;
    }

    m_signalHintLabel->setText(QString("消息: %1 (%2) — %3 个信号")
        .arg(m_currentDbcMsg->name)
        .arg(formatIdHex(m_currentDbcMsg->id))
        .arg(m_currentDbcMsg->signalList.size()));

    // Remove the stretch
    m_signalLayout->takeAt(m_signalLayout->count() - 1);

    QByteArray data = parseHexData(m_dataEdit->text());
    while (data.size() < m_currentDbcMsg->dlc)
        data.append(static_cast<char>(0));

    for (const auto &sig : m_currentDbcMsg->signalList) {
        auto *rowWidget = new QWidget();
        auto *rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *nameLabel = new QLabel(sig.name);
        nameLabel->setMinimumWidth(140);
        nameLabel->setStyleSheet("font-weight: bold;");
        rowLayout->addWidget(nameLabel);

        auto *spin = new QDoubleSpinBox();
        spin->setRange(sig.minimum, sig.maximum);
        spin->setSingleStep(sig.factor > 0 ? sig.factor : 1.0);
        spin->setMinimumWidth(120);

        // Calculate decimals from factor
        int decimals = 0;
        double f = sig.factor;
        while (f < 1.0 && decimals < 6) {
            f *= 10;
            decimals++;
        }
        spin->setDecimals(decimals);

        double val = sig.decode(data);
        spin->setValue(val);
        rowLayout->addWidget(spin);

        auto *unitLabel = new QLabel(sig.unit.isEmpty() ? "" : sig.unit);
        unitLabel->setMinimumWidth(40);
        rowLayout->addWidget(unitLabel);

        auto *rangeLabel = new QLabel(
            QString("(%1 ~ %2)").arg(sig.minimum).arg(sig.maximum));
        rangeLabel->setStyleSheet("color: gray; font-size: 11px;");
        rowLayout->addWidget(rangeLabel);
        rowLayout->addStretch();

        m_signalLayout->addWidget(rowWidget);
        m_signalSpinBoxes.append(spin);

        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &SignalSendTab::onSignalValueChanged);
    }
    m_signalLayout->addStretch();
}

void SignalSendTab::onSignalValueChanged()
{
    if (m_updatingSignals)
        return;
    updateDataFromSignals();
}

void SignalSendTab::updateDataFromSignals()
{
    if (!m_currentDbcMsg || m_signalSpinBoxes.isEmpty())
        return;

    QByteArray data = parseHexData(m_dataEdit->text());
    while (data.size() < m_currentDbcMsg->dlc)
        data.append(static_cast<char>(0));

    for (int i = 0; i < m_signalSpinBoxes.size() && i < m_currentDbcMsg->signalList.size(); ++i) {
        m_currentDbcMsg->signalList[i].encode(data, m_signalSpinBoxes[i]->value());
    }

    m_dataEdit->setText(formatDataHex(data));
}

void SignalSendTab::onDataEditFinished()
{
    updateSignalsFromData();
}

void SignalSendTab::updateSignalsFromData()
{
    if (!m_currentDbcMsg || m_signalSpinBoxes.isEmpty())
        return;

    QByteArray data = parseHexData(m_dataEdit->text());
    while (data.size() < m_currentDbcMsg->dlc)
        data.append(static_cast<char>(0));

    m_updatingSignals = true;
    for (int i = 0; i < m_signalSpinBoxes.size() && i < m_currentDbcMsg->signalList.size(); ++i) {
        QSignalBlocker blocker(m_signalSpinBoxes[i]);
        double val = m_currentDbcMsg->signalList[i].decode(data);
        m_signalSpinBoxes[i]->setValue(val);
    }
    m_updatingSignals = false;
}

// ============================================================
//  Send list table operations
// ============================================================

QWidget *SignalSendTab::createOpWidget()
{
    auto *widget = new QWidget();
    auto *layout = new QHBoxLayout(widget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    auto *sendBtn = new QPushButton("▶", widget);
    auto *stopBtn = new QPushButton("⏹", widget);
    auto *delBtn = new QPushButton("✕", widget);
    auto *upBtn = new QPushButton("↑", widget);
    auto *downBtn = new QPushButton("↓", widget);

    sendBtn->setToolTip("发送");
    stopBtn->setToolTip("停止");
    delBtn->setToolTip("删除");
    upBtn->setToolTip("上移");
    downBtn->setToolTip("下移");

    for (auto *btn : {sendBtn, stopBtn, delBtn, upBtn, downBtn}) {
        btn->setFixedWidth(26);
        btn->setFixedHeight(24);
        layout->addWidget(btn);
    }

    connect(sendBtn, &QPushButton::clicked, this, [this, sendBtn]() {
        int row = findRowOfButton(sendBtn);
        if (row >= 0) onRowSend(row);
    });
    connect(stopBtn, &QPushButton::clicked, this, [this, stopBtn]() {
        int row = findRowOfButton(stopBtn);
        if (row >= 0) onRowStop(row);
    });
    connect(delBtn, &QPushButton::clicked, this, [this, delBtn]() {
        int row = findRowOfButton(delBtn);
        if (row >= 0) onRowDelete(row);
    });
    connect(upBtn, &QPushButton::clicked, this, [this, upBtn]() {
        int row = findRowOfButton(upBtn);
        if (row >= 0) onRowMoveUp(row);
    });
    connect(downBtn, &QPushButton::clicked, this, [this, downBtn]() {
        int row = findRowOfButton(downBtn);
        if (row >= 0) onRowMoveDown(row);
    });

    return widget;
}

int SignalSendTab::findRowOfButton(QWidget *btn) const
{
    QWidget *opWidget = btn->parentWidget();
    for (int i = 0; i < m_sendTable->rowCount(); ++i) {
        if (m_sendTable->cellWidget(i, 9) == opWidget)
            return i;
    }
    return -1;
}

void SignalSendTab::renumberRows()
{
    for (int i = 0; i < m_sendTable->rowCount(); ++i) {
        auto *item = m_sendTable->item(i, 1);
        if (item)
            item->setText(QString::number(i + 1));
    }
}

void SignalSendTab::onAddToList()
{
    QString idText = m_idEdit->text().trimmed();
    quint32 id = idText.toUInt(nullptr, 16);
    if (id == 0 && !idText.isEmpty() && !idText.startsWith("0x", Qt::CaseInsensitive))
        id = idText.toUInt();

    int dlc = m_dlcSpin->value();
    QString dataHex = m_dataEdit->text().trimmed();
    int period = m_periodSpin->value();
    int count = m_countSpin->value();

    // Look up DBC message name
    QString name;
    if (m_dbcManager && id != 0) {
        auto *msg = m_dbcManager->findMessage(id);
        if (msg)
            name = msg->name;
    }

    int row = m_sendTable->rowCount();
    m_sendTable->insertRow(row);

    // Checkbox (checked by default)
    auto *checkItem = new QTableWidgetItem;
    checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    checkItem->setCheckState(Qt::Checked);
    m_sendTable->setItem(row, 0, checkItem);

    m_sendTable->setItem(row, 1, new QTableWidgetItem(QString::number(row + 1)));
    m_sendTable->setItem(row, 2, new QTableWidgetItem(formatIdHex(id)));
    m_sendTable->setItem(row, 3, new QTableWidgetItem(name));
    m_sendTable->setItem(row, 4, new QTableWidgetItem(QString::number(dlc)));
    m_sendTable->setItem(row, 5, new QTableWidgetItem(dataHex));
    m_sendTable->setItem(row, 6, new QTableWidgetItem(QString::number(period)));
    m_sendTable->setItem(row, 7, new QTableWidgetItem(count == 0 ? "∞" : QString::number(count)));
    m_sendTable->setItem(row, 8, new QTableWidgetItem("就绪"));

    m_sendTable->setCellWidget(row, 9, createOpWidget());
}

void SignalSendTab::onSendSingle()
{
    QString idText = m_idEdit->text().trimmed();
    quint32 id = idText.toUInt(nullptr, 16);
    if (id == 0 && !idText.isEmpty() && !idText.startsWith("0x", Qt::CaseInsensitive))
        id = idText.toUInt();
    QByteArray data = parseHexData(m_dataEdit->text());
    emit sendSingleRequested(id, data);
}

void SignalSendTab::onSendAll()
{
    for (int i = 0; i < m_sendTable->rowCount(); ++i) {
        auto *checkItem = m_sendTable->item(i, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked)
            onRowSend(i);
    }
}

void SignalSendTab::onStopAll()
{
    for (int i = 0; i < m_sendTable->rowCount(); ++i) {
        auto *statusItem = m_sendTable->item(i, 8);
        if (statusItem && statusItem->text() == "发送中")
            onRowStop(i);
    }
    emit stopAllRequested();
}

void SignalSendTab::onClearList()
{
    m_sendTable->setRowCount(0);
}

void SignalSendTab::onImportFromDbc()
{
    if (!m_dbcManager)
        return;

    const auto messages = m_dbcManager->allMessages();
    for (const auto *msg : messages) {
        int row = m_sendTable->rowCount();
        m_sendTable->insertRow(row);

        auto *checkItem = new QTableWidgetItem;
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Checked);
        m_sendTable->setItem(row, 0, checkItem);

        m_sendTable->setItem(row, 1, new QTableWidgetItem(QString::number(row + 1)));
        m_sendTable->setItem(row, 2, new QTableWidgetItem(formatIdHex(msg->id)));
        m_sendTable->setItem(row, 3, new QTableWidgetItem(msg->name));
        m_sendTable->setItem(row, 4, new QTableWidgetItem(QString::number(msg->dlc)));

        QByteArray data(msg->dlc, '\0');
        m_sendTable->setItem(row, 5, new QTableWidgetItem(formatDataHex(data)));

        int period = msg->cycleTime > 0 ? msg->cycleTime : 100;
        m_sendTable->setItem(row, 6, new QTableWidgetItem(QString::number(period)));
        m_sendTable->setItem(row, 7, new QTableWidgetItem("∞"));
        m_sendTable->setItem(row, 8, new QTableWidgetItem("就绪"));

        m_sendTable->setCellWidget(row, 9, createOpWidget());
    }
}

// ============================================================
//  Per-row operations
// ============================================================

void SignalSendTab::onRowSend(int row)
{
    if (row < 0 || row >= m_sendTable->rowCount())
        return;

    QString idStr = m_sendTable->item(row, 2)->text();
    idStr.remove("0x", Qt::CaseInsensitive);
    quint32 id = idStr.trimmed().toUInt(nullptr, 16);

    QByteArray data = parseHexData(m_sendTable->item(row, 5)->text());

    QString periodStr = m_sendTable->item(row, 6)->text();
    int period = periodStr.isEmpty() ? 0 : periodStr.toInt();

    QString countStr = m_sendTable->item(row, 7)->text();
    int count = (countStr == "∞") ? 0 : countStr.toInt();

    m_sendTable->item(row, 8)->setText("发送中");

    emit sendRowRequested(row, id, data, period, count);
}

void SignalSendTab::onRowStop(int row)
{
    if (row < 0 || row >= m_sendTable->rowCount())
        return;

    m_sendTable->item(row, 8)->setText("已停止");
    emit stopRowRequested(row);
}

void SignalSendTab::onRowDelete(int row)
{
    if (row < 0 || row >= m_sendTable->rowCount())
        return;

    m_sendTable->removeRow(row);
    renumberRows();
}

void SignalSendTab::onRowMoveUp(int row)
{
    if (row <= 0)
        return;

    // Swap all cell items except # (column 1, renumbered later)
    for (int col = 0; col <= 8; ++col) {
        if (col == 1)
            continue;

        auto *item1 = m_sendTable->item(row, col);
        auto *item2 = m_sendTable->item(row - 1, col);
        if (item1 && item2) {
            QString text = item1->text();
            item1->setText(item2->text());
            item2->setText(text);

            if (col == 0) {
                Qt::CheckState state = item1->checkState();
                item1->setCheckState(item2->checkState());
                item2->setCheckState(state);
            }
        }
    }

    renumberRows();
    m_sendTable->selectRow(row - 1);
}

void SignalSendTab::onRowMoveDown(int row)
{
    if (row >= m_sendTable->rowCount() - 1)
        return;
    onRowMoveUp(row + 1);
}
