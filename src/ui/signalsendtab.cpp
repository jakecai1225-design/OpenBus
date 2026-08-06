#include "signalsendtab.h"
#include "dbcimportdialog.h"
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

    // ---- 顶部工具栏 ----
    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->setSpacing(4);
    m_sendAllBtn = new QPushButton("列表发送", this);
    m_stopAllBtn = new QPushButton("列表停止", this);
    m_clearListBtn = new QPushButton("清空列表", this);
    toolbarLayout->addWidget(m_sendAllBtn);
    toolbarLayout->addWidget(m_stopAllBtn);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_clearListBtn);
    mainLayout->addLayout(toolbarLayout);

    // ---- Splitter: table (top) + edit area (bottom) ----
    auto *splitter = new QSplitter(Qt::Vertical, this);

    // -- 发送列表表格 --
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
    m_sendTable->setSelectionMode(QAbstractItemView::SingleSelection);

    // -- 底部编辑区 --
    auto *editGroup = new QGroupBox("编辑发送帧", splitter);
    auto *editLayout = new QVBoxLayout(editGroup);
    editLayout->setContentsMargins(8, 8, 8, 8);
    editLayout->setSpacing(6);

    // Row 1: 从DBC导入, ID, DLC, 数据(Hex)
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(6);
    m_importDbcBtn = new QPushButton("从DBC导入", editGroup);
    row1->addWidget(m_importDbcBtn);

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

    // Row 2: 周期, 次数, 按钮
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

    row2->addStretch();
    m_addToListBtn = new QPushButton("添加到列表", editGroup);
    m_sendSingleBtn = new QPushButton("发送单帧", editGroup);
    row2->addWidget(m_addToListBtn);
    row2->addWidget(m_sendSingleBtn);
    editLayout->addLayout(row2);

    // 信号级编辑区域
    m_signalHintLabel = new QLabel("输入 CAN ID 或从发送列表选择一行，自动显示信号编辑器", editGroup);
    m_signalHintLabel->setObjectName("DbcDetailComment");
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
    splitter->setSizes({250, 400});
    mainLayout->addWidget(splitter, 1);

    // ---- 连接 ----
    connect(m_sendAllBtn, &QPushButton::clicked, this, &SignalSendTab::onSendAll);
    connect(m_stopAllBtn, &QPushButton::clicked, this, &SignalSendTab::onStopAll);
    connect(m_clearListBtn, &QPushButton::clicked, this, &SignalSendTab::onClearList);
    connect(m_importDbcBtn, &QPushButton::clicked, this, &SignalSendTab::onImportFromDbc);
    connect(m_addToListBtn, &QPushButton::clicked, this, &SignalSendTab::onAddToList);
    connect(m_sendSingleBtn, &QPushButton::clicked, this, &SignalSendTab::onSendSingle);
    connect(m_idEdit, &QLineEdit::editingFinished, this, &SignalSendTab::onIdEditingFinished);
    connect(m_dataEdit, &QLineEdit::editingFinished, this, &SignalSendTab::onDataEditFinished);
    connect(m_sendTable, &QTableWidget::itemSelectionChanged,
            this, &SignalSendTab::onSendTableRowChanged);
}

// ============================================================
//  DbcManager integration
// ============================================================

void SignalSendTab::setDbcManager(DbcManager *mgr)
{
    m_dbcManager = mgr;
}

// ============================================================
//  Table row selection → sync bottom editor
// ============================================================

void SignalSendTab::onSendTableRowChanged()
{
    int row = m_sendTable->currentRow();
    if (row < 0 || row >= m_sendTable->rowCount()) {
        m_selectedRow = -1;
        return;
    }
    m_selectedRow = row;
    loadRowToEditor(row);
}

void SignalSendTab::loadRowToEditor(int row)
{
    if (row < 0 || row >= m_sendTable->rowCount())
        return;

    // 读取行数据填入编辑器
    QString idStr = m_sendTable->item(row, 2)->text();
    m_idEdit->setText(idStr);

    QString dlcStr = m_sendTable->item(row, 4)->text();
    m_dlcSpin->setValue(dlcStr.toInt());

    QString dataHex = m_sendTable->item(row, 5)->text();
    m_dataEdit->setText(dataHex);

    QString periodStr = m_sendTable->item(row, 6)->text();
    m_periodSpin->setValue(periodStr.isEmpty() ? 0 : periodStr.toInt());

    QString countStr = m_sendTable->item(row, 7)->text();
    m_countSpin->setValue(countStr == "∞" ? 0 : countStr.toInt());

    // 触发信号编辑器重建
    onIdEditingFinished();
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
    } else if (msg) {
        // 同一消息，但数据可能变了，更新信号编辑器
        updateSignalsFromData();
    }
}

void SignalSendTab::rebuildSignalEditors()
{
    // 清除现有编辑器
    QLayoutItem *item;
    while ((item = m_signalLayout->takeAt(0)) != nullptr) {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_signalLayout->addStretch();
    m_signalWidgets.clear();

    if (!m_dbcManager || !m_currentDbcMsg) {
        m_signalHintLabel->setText("输入 CAN ID 或从发送列表选择一行，自动显示信号编辑器");
        return;
    }

    m_signalHintLabel->setText(QString("消息: %1 (%2) — %3 个信号")
        .arg(m_currentDbcMsg->name)
        .arg(formatIdHex(m_currentDbcMsg->id))
        .arg(m_currentDbcMsg->signalList.size()));

    // 移除 stretch
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
        QFont boldFont = nameLabel->font();
        boldFont.setBold(true);
        nameLabel->setFont(boldFont);
        rowLayout->addWidget(nameLabel);

        if (!sig.valueTable.isEmpty()) {
            // 枚举信号 → QComboBox
            auto *combo = new QComboBox();
            combo->setMinimumWidth(200);
            for (const auto &vd : sig.valueTable) {
                QString itemText = QString("%1 = %2").arg(vd.value).arg(vd.description);
                combo->addItem(itemText, vd.value);
            }
            // 设置当前值
            double physVal = sig.decode(data);
            quint64 rawVal = sig.rawDecode(data);
            int bestIdx = 0;
            for (int i = 0; i < combo->count(); ++i) {
                if (combo->itemData(i).toInt() == static_cast<int>(rawVal)) {
                    bestIdx = i;
                    break;
                }
            }
            combo->setCurrentIndex(bestIdx);
            rowLayout->addWidget(combo);
            m_signalWidgets.append(combo);

            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, &SignalSendTab::onSignalValueChanged);
        } else {
            // 数值信号 → QDoubleSpinBox
            auto *spin = new QDoubleSpinBox();
            spin->setRange(sig.minimum, sig.maximum);
            spin->setSingleStep(sig.factor > 0 ? sig.factor : 1.0);
            spin->setMinimumWidth(120);

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
            m_signalWidgets.append(spin);

            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, &SignalSendTab::onSignalValueChanged);
        }

        auto *unitLabel = new QLabel(sig.unit.isEmpty() ? "" : sig.unit);
        unitLabel->setMinimumWidth(40);
        rowLayout->addWidget(unitLabel);

        auto *rangeLabel = new QLabel(
            QString("(%1 ~ %2)").arg(sig.minimum).arg(sig.maximum));
        rangeLabel->setObjectName("DimLabel");
        rowLayout->addWidget(rangeLabel);
        rowLayout->addStretch();

        m_signalLayout->addWidget(rowWidget);
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
    if (!m_currentDbcMsg || m_signalWidgets.isEmpty())
        return;

    QByteArray data = parseHexData(m_dataEdit->text());
    while (data.size() < m_currentDbcMsg->dlc)
        data.append(static_cast<char>(0));

    for (int i = 0; i < m_signalWidgets.size() && i < m_currentDbcMsg->signalList.size(); ++i) {
        const auto &sig = m_currentDbcMsg->signalList[i];

        if (auto *combo = qobject_cast<QComboBox *>(m_signalWidgets[i])) {
            // 枚举信号：取 rawValue，转为物理值后编码
            int rawVal = combo->currentData().toInt();
            double physVal = sig.rawToPhys(static_cast<quint64>(rawVal));
            sig.encode(data, physVal);
        } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(m_signalWidgets[i])) {
            sig.encode(data, spin->value());
        }
    }

    // 更新数据 Hex 显示（不触发 editingFinished）
    QSignalBlocker blocker(m_dataEdit);
    m_dataEdit->setText(formatDataHex(data));

    // 如果选中了发送列表的某行，同步更新该行数据
    if (m_selectedRow >= 0 && m_selectedRow < m_sendTable->rowCount()) {
        auto *dataItem = m_sendTable->item(m_selectedRow, 5);
        if (dataItem)
            dataItem->setText(formatDataHex(data));
    }
}

void SignalSendTab::onDataEditFinished()
{
    updateSignalsFromData();
}

void SignalSendTab::updateSignalsFromData()
{
    if (!m_currentDbcMsg || m_signalWidgets.isEmpty())
        return;

    QByteArray data = parseHexData(m_dataEdit->text());
    while (data.size() < m_currentDbcMsg->dlc)
        data.append(static_cast<char>(0));

    m_updatingSignals = true;
    for (int i = 0; i < m_signalWidgets.size() && i < m_currentDbcMsg->signalList.size(); ++i) {
        const auto &sig = m_currentDbcMsg->signalList[i];

        if (auto *combo = qobject_cast<QComboBox *>(m_signalWidgets[i])) {
            quint64 rawVal = sig.rawDecode(data);
            bool found = false;
            for (int j = 0; j < combo->count(); ++j) {
                if (combo->itemData(j).toInt() == static_cast<int>(rawVal)) {
                    QSignalBlocker blocker(combo);
                    combo->setCurrentIndex(j);
                    found = true;
                    break;
                }
            }
            if (!found) {
                QSignalBlocker blocker(combo);
                combo->setCurrentIndex(-1);
            }
        } else if (auto *spin = qobject_cast<QDoubleSpinBox *>(m_signalWidgets[i])) {
            QSignalBlocker blocker(spin);
            double val = sig.decode(data);
            spin->setValue(val);
        }
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

    auto *sendBtn = new QPushButton("发送", widget);
    auto *stopBtn = new QPushButton("停止", widget);
    auto *delBtn = new QPushButton("×", widget);
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

    // 查找 DBC 消息名称
    QString name;
    if (m_dbcManager && id != 0) {
        auto *msg = m_dbcManager->findMessage(id);
        if (msg)
            name = msg->name;
    }

    // 如果有选中行且 CAN ID 相同，更新该行
    if (m_selectedRow >= 0 && m_selectedRow < m_sendTable->rowCount()) {
        QString existingId = m_sendTable->item(m_selectedRow, 2)->text();
        QString existingIdClean = existingId;
        existingIdClean.remove("0x", Qt::CaseInsensitive);
        quint32 existingIdVal = existingIdClean.trimmed().toUInt(nullptr, 16);
        if (existingIdVal == id) {
            // 更新现有行
            m_sendTable->item(m_selectedRow, 4)->setText(QString::number(dlc));
            m_sendTable->item(m_selectedRow, 5)->setText(dataHex);
            m_sendTable->item(m_selectedRow, 6)->setText(QString::number(period));
            m_sendTable->item(m_selectedRow, 7)->setText(count == 0 ? "∞" : QString::number(count));
            return;
        }
    }

    // 添加新行
    int row = m_sendTable->rowCount();
    m_sendTable->insertRow(row);

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
    m_sendTable->selectRow(row);
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
    m_selectedRow = -1;
}

void SignalSendTab::onImportFromDbc()
{
    if (!m_dbcManager) {
        m_signalHintLabel->setText("未设置 DBC 管理器");
        return;
    }

    DbcImportDialog dlg(m_dbcManager, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto ids = dlg.selectedCanIds();
    for (quint32 id : ids) {
        const DbcMessage *msg = m_dbcManager->findMessage(id);
        if (!msg)
            continue;

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

    // 选中第一行新增的条目
    if (m_sendTable->rowCount() > 0)
        m_sendTable->selectRow(0);
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
    if (m_selectedRow == row)
        m_selectedRow = -1;
}

void SignalSendTab::onRowMoveUp(int row)
{
    if (row <= 0)
        return;

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
