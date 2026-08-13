#include "canopenview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QDateTime>
#include <QSplitter>

// ============================================================
//  构造
// ============================================================

CanOpenView::CanOpenView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void CanOpenView::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    mainLayout->addWidget(splitter);

    // ---- 上部：功能标签页 ----
    auto *tabWidget = new QTabWidget(this);
    tabWidget->addTab(createNmtTab(), "NMT 节点控制");
    tabWidget->addTab(createSdoTab(), "SDO 读写");
    tabWidget->addTab(createEmergencyTab(), "Emergency");
    tabWidget->addTab(createHeartbeatTab(), "Heartbeat");
    splitter->addWidget(tabWidget);

    // ---- 下部：事务日志 ----
    auto *logGroup = new QGroupBox("CANopen 事务日志", this);
    auto *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(2, 2, 2, 2);

    m_logTable = new QTableWidget(0, 4, logGroup);
    m_logTable->setHorizontalHeaderLabels({"时间", "类型", "描述", "数据/状态"});
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_logTable->setColumnWidth(0, 100);
    m_logTable->setColumnWidth(1, 80);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logLayout->addWidget(m_logTable);

    auto *btnLayout = new QHBoxLayout();
    auto *clearBtn = new QPushButton(" 清空日志", logGroup);
    btnLayout->addStretch();
    btnLayout->addWidget(clearBtn);
    logLayout->addLayout(btnLayout);

    connect(clearBtn, &QPushButton::clicked, this, &CanOpenView::onClearLog);

    splitter->addWidget(logGroup);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
}

// ============================================================
//  NMT 标签页
// ============================================================

QWidget *CanOpenView::createNmtTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QFormLayout(widget);

    // 节点 ID
    auto *idLayout = new QHBoxLayout();
    m_nmtNodeId = new QSpinBox(widget);
    m_nmtNodeId->setRange(0, 127);
    m_nmtNodeId->setValue(1);
    idLayout->addWidget(new QLabel("节点 ID:"));
    idLayout->addWidget(m_nmtNodeId);
    idLayout->addStretch();
    layout->addRow("", idLayout);

    // NMT 命令按钮
    auto *cmdGroup = new QGroupBox("NMT 命令", widget);
    auto *cmdLayout = new QVBoxLayout(cmdGroup);

    // NMT 命令定义
    struct NmtCmd { int cmd; const char *name; const char *desc; };
    static const NmtCmd cmds[] = {
        {0x01, " Start",          "启动节点 (进入 Operational)"},
        {0x02, "■ Stop",           "停止节点 (进入 Stopped)"},
        {0x80, "⏸ Pre-Operational", "进入预操作状态"},
        {0x81, "↻ Reset Node",     "复位节点 (应用层复位)"},
        {0x82, "↻ Reset Comm",     "复位通信 (通信参数复位)"},
    };

    for (const auto &cmd : cmds) {
        auto *btn = new QPushButton(QString::fromUtf8(cmd.name), cmdGroup);
        btn->setToolTip(QString::fromUtf8(cmd.desc));
        btn->setProperty("nmtCmd", cmd.cmd);
        cmdLayout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, cmd]() {
            onNmtCommand(cmd.cmd);
        });
    }

    // 全局命令 (节点 ID = 0)
    auto *allBtn = new QPushButton("🌐 广播 Start (所有节点)", cmdGroup);
    allBtn->setProperty("nmtCmd", 0x01);
    cmdLayout->addWidget(allBtn);
    connect(allBtn, &QPushButton::clicked, this, [this]() {
        int savedId = m_nmtNodeId->value();
        m_nmtNodeId->setValue(0);
        onNmtCommand(0x01);
        m_nmtNodeId->setValue(savedId);
    });

    layout->addRow(cmdGroup);

    // 状态显示
    m_nmtStatusLabel = new QLabel("状态: 未知", widget);
    m_nmtStatusLabel->setObjectName("NmtStatus");
    layout->addRow("当前状态:", m_nmtStatusLabel);

    return widget;
}

// ============================================================
//  SDO 标签页
// ============================================================

QWidget *CanOpenView::createSdoTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QFormLayout(widget);

    // 节点 ID
    m_sdoNodeId = new QSpinBox(widget);
    m_sdoNodeId->setRange(1, 127);
    m_sdoNodeId->setValue(1);
    layout->addRow("节点 ID:", m_sdoNodeId);

    // Index
    m_sdoIndex = new QSpinBox(widget);
    m_sdoIndex->setRange(0, 0xFFFF);
    m_sdoIndex->setValue(0x1000);
    m_sdoIndex->setDisplayIntegerBase(16);
    m_sdoIndex->setPrefix("0x");
    layout->addRow("Index:", m_sdoIndex);

    // Subindex
    m_sdoSubIndex = new QSpinBox(widget);
    m_sdoSubIndex->setRange(0, 0xFF);
    m_sdoSubIndex->setValue(0x00);
    m_sdoSubIndex->setDisplayIntegerBase(16);
    m_sdoSubIndex->setPrefix("0x");
    layout->addRow("Subindex:", m_sdoSubIndex);

    // 数据类型
    m_sdoDataType = new QComboBox(widget);
    m_sdoDataType->addItem("U8  (1 byte)", 0x0F);
    m_sdoDataType->addItem("U16 (2 bytes)", 0x0B);
    m_sdoDataType->addItem("U32 (4 bytes)", 0x07);
    m_sdoDataType->addItem("I8  (1 byte)", 0x0E);
    m_sdoDataType->addItem("I16 (2 bytes)", 0x0A);
    m_sdoDataType->addItem("I32 (4 bytes)", 0x04);
    m_sdoDataType->addItem("String", 0x09);
    m_sdoDataType->addItem("Domain", 0x02);
    layout->addRow("数据类型:", m_sdoDataType);

    // 数据
    m_sdoDataEdit = new QLineEdit(widget);
    m_sdoDataEdit->setPlaceholderText("写入数据 (hex: 01 02 03 04)");
    layout->addRow("数据:", m_sdoDataEdit);

    // 按钮
    auto *btnLayout = new QHBoxLayout();
    auto *readBtn = new QPushButton("📖 SDO 读", widget);
    auto *writeBtn = new QPushButton("✏ SDO 写", widget);
    readBtn->setObjectName("primaryBtn");
    btnLayout->addWidget(readBtn);
    btnLayout->addWidget(writeBtn);
    btnLayout->addStretch();
    layout->addRow("", btnLayout);

    connect(readBtn, &QPushButton::clicked, this, &CanOpenView::onSdoRead);
    connect(writeBtn, &QPushButton::clicked, this, &CanOpenView::onSdoWrite);

    // 结果
    m_sdoResultLabel = new QLabel("就绪", widget);
    m_sdoResultLabel->setObjectName("NmtStatus");
    m_sdoResultLabel->setWordWrap(true);
    layout->addRow("结果:", m_sdoResultLabel);

    // 常用 OD 条目快速选择
    auto *odGroup = new QGroupBox("常用对象字典", widget);
    auto *odLayout = new QVBoxLayout(odGroup);

    struct OdEntry { int idx; const char *name; };
    static const OdEntry odEntries[] = {
        {0x1000, "1000 device type"},
        {0x1001, "1001 error register"},
        {0x1010, "1010 store parameters"},
        {0x1011, "1011 restore default"},
        {0x1018, "1018 identity"},
        {0x1200, "1200 SDO server param"},
        {0x1400, "1400 RX PDO param"},
        {0x1800, "1800 TX PDO param"},
        {0x6040, "6040 control word"},
        {0x6041, "6041 status word"},
        {0x6064, "6064 position actual"},
        {0x607A, "607A target position"},
    };

    for (const auto &entry : odEntries) {
        auto *btn = new QPushButton(QString::fromUtf8(entry.name), odGroup);
        btn->setProperty("odIdx", entry.idx);
        odLayout->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, entry]() {
            m_sdoIndex->setValue(entry.idx);
        });
    }

    layout->addRow(odGroup);

    return widget;
}

// ============================================================
//  Emergency 标签页
// ============================================================

QWidget *CanOpenView::createEmergencyTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);

    m_emergencyTable = new QTableWidget(0, 6, widget);
    m_emergencyTable->setHorizontalHeaderLabels({
        "时间", "节点 ID", "错误码", "错误寄存器", "数据", "描述"
    });
    m_emergencyTable->horizontalHeader()->setStretchLastSection(true);
    m_emergencyTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_emergencyTable->setColumnWidth(0, 100);
    m_emergencyTable->setColumnWidth(1, 60);
    m_emergencyTable->setColumnWidth(2, 80);
    m_emergencyTable->setColumnWidth(3, 80);
    m_emergencyTable->setColumnWidth(4, 120);
    m_emergencyTable->setAlternatingRowColors(true);
    m_emergencyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_emergencyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_emergencyTable);

    auto *btnLayout = new QHBoxLayout();
    auto *addDemoBtn = new QPushButton("添加模拟 Emergency", widget);
    auto *clearBtn = new QPushButton(" 清空", widget);
    btnLayout->addWidget(addDemoBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(clearBtn);
    layout->addLayout(btnLayout);

    // 模拟 Emergency 数据
    connect(addDemoBtn, &QPushButton::clicked, this, [this]() {
        int row = m_emergencyTable->rowCount();
        m_emergencyTable->insertRow(row);

        QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
        m_emergencyTable->setItem(row, 0, new QTableWidgetItem(timeStr));
        m_emergencyTable->setItem(row, 1, new QTableWidgetItem("0x01"));
        m_emergencyTable->setItem(row, 2, new QTableWidgetItem("0x1000"));
        m_emergencyTable->setItem(row, 3, new QTableWidgetItem("0x01"));
        m_emergencyTable->setItem(row, 4, new QTableWidgetItem("00 00 00 00"));
        m_emergencyTable->setItem(row, 5, new QTableWidgetItem("Generic error"));

        m_emergencyTable->scrollToBottom();
    });

    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_emergencyTable->setRowCount(0);
    });

    return widget;
}

// ============================================================
//  Heartbeat 标签页
// ============================================================

QWidget *CanOpenView::createHeartbeatTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);

    auto *infoLabel = new QLabel(
        "Heartbeat 消息用于监控节点在线状态。\n"
        "生产者周期性发送 (COB-ID: 0x700 + NodeID)，消费者超时检测。",
        widget);
    infoLabel->setWordWrap(true);
    infoLabel->setObjectName("SidePanelHint");
    layout->addWidget(infoLabel);

    m_heartbeatTable = new QTableWidget(0, 5, widget);
    m_heartbeatTable->setHorizontalHeaderLabels({
        "节点 ID", "COB-ID", "周期 (ms)", "上次收到", "状态"
    });
    m_heartbeatTable->horizontalHeader()->setStretchLastSection(true);
    m_heartbeatTable->setColumnWidth(0, 80);
    m_heartbeatTable->setColumnWidth(1, 80);
    m_heartbeatTable->setColumnWidth(2, 80);
    m_heartbeatTable->setColumnWidth(3, 120);
    m_heartbeatTable->setAlternatingRowColors(true);
    m_heartbeatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_heartbeatTable);

    auto *btnLayout = new QHBoxLayout();
    auto *addBtn = new QPushButton(" 添加节点监控", widget);
    auto *clearBtn = new QPushButton(" 清空", widget);
    btnLayout->addWidget(addBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(clearBtn);
    layout->addLayout(btnLayout);

    // 模拟添加节点
    int demoNode = 1;
    connect(addBtn, &QPushButton::clicked, this, [this, demoNode]() mutable {
        int row = m_heartbeatTable->rowCount();
        m_heartbeatTable->insertRow(row);
        m_heartbeatTable->setItem(row, 0, new QTableWidgetItem(QString("0x%1").arg(demoNode, 2, 16, QChar('0')).toUpper()));
        m_heartbeatTable->setItem(row, 1, new QTableWidgetItem(QString("0x%1").arg(0x700 + demoNode, 3, 16, QChar('0')).toUpper()));
        m_heartbeatTable->setItem(row, 2, new QTableWidgetItem("1000"));
        m_heartbeatTable->setItem(row, 3, new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss")));
        m_heartbeatTable->setItem(row, 4, new QTableWidgetItem("在线"));

        // 绿色标记在线
        m_heartbeatTable->item(row, 4)->setForeground(QColor(0x4C, 0xAF, 0x50));
        demoNode++;
    });

    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_heartbeatTable->setRowCount(0);
    });

    return widget;
}

// ============================================================
//  事件处理
// ============================================================

void CanOpenView::onNmtCommand(int cmd)
{
    int nodeId = m_nmtNodeId->value();

    QString cmdName;
    QString statusText;
    switch (cmd) {
    case 0x01: cmdName = "Start"; statusText = "Operational"; break;
    case 0x02: cmdName = "Stop"; statusText = "Stopped"; break;
    case 0x80: cmdName = "Pre-Operational"; statusText = "Pre-Operational"; break;
    case 0x81: cmdName = "Reset Node"; statusText = "Resetting..."; break;
    case 0x82: cmdName = "Reset Communication"; statusText = "Resetting Comm..."; break;
    default: cmdName = QString("Unknown(0x%1)").arg(cmd, 2, 16, QChar('0')).toUpper(); break;
    }

    QString target = (nodeId == 0) ? "所有节点" : QString("节点 0x%1").arg(nodeId, 2, 16, QChar('0')).toUpper();
    QString data = QString("CS=0x%1, NodeID=%2").arg(cmd, 2, 16, QChar('0')).toUpper()
                       .arg(nodeId, 2, 16, QChar('0')).toUpper();

    appendLog("NMT", QString("%1 → %2").arg(cmdName).arg(target), data, "已发送");

    m_nmtStatusLabel->setText(QString("状态: %1").arg(statusText));
    m_nmtStatusLabel->setObjectName("StatusSuccess");
}

void CanOpenView::onSdoRead()
{
    int nodeId = m_sdoNodeId->value();
    int index = m_sdoIndex->value();
    int subIdx = m_sdoSubIndex->value();

    QString desc = QString("节点 0x%1 读 0x%2:0x%3")
                       .arg(nodeId, 2, 16, QChar('0')).toUpper()
                       .arg(index, 4, 16, QChar('0')).toUpper()
                       .arg(subIdx, 2, 16, QChar('0')).toUpper();

    appendLog("SDO 读", desc, "请求已发送", "等待响应...");

    // 模拟响应
    QString simData = "12 34 56 78";
    m_sdoResultLabel->setText(QString("读取成功: %1").arg(simData));
    m_sdoResultLabel->setObjectName("StatusSuccess");

    appendLog("SDO 响应", desc, simData, "成功");
}

void CanOpenView::onSdoWrite()
{
    int nodeId = m_sdoNodeId->value();
    int index = m_sdoIndex->value();
    int subIdx = m_sdoSubIndex->value();
    QString data = m_sdoDataEdit->text().trimmed();

    if (data.isEmpty()) {
        m_sdoResultLabel->setText("错误: 请输入要写入的数据");
        m_sdoResultLabel->setObjectName("StatusError");
        return;
    }

    QString desc = QString("节点 0x%1 写 0x%2:0x%3 = %4")
                       .arg(nodeId, 2, 16, QChar('0')).toUpper()
                       .arg(index, 4, 16, QChar('0')).toUpper()
                       .arg(subIdx, 2, 16, QChar('0')).toUpper()
                       .arg(data);

    appendLog("SDO 写", desc, "请求已发送", "等待响应...");

    // 模拟成功
    m_sdoResultLabel->setText("写入成功");
    m_sdoResultLabel->setObjectName("StatusSuccess");

    appendLog("SDO 响应", desc, "", "成功");
}

void CanOpenView::onClearLog()
{
    m_logTable->setRowCount(0);
}

// ============================================================
//  辅助方法
// ============================================================

void CanOpenView::appendLog(const QString &type, const QString &desc,
                            const QString &data, const QString &status)
{
    int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    auto *item0 = new QTableWidgetItem(timeStr);
    auto *item1 = new QTableWidgetItem(type);
    auto *item2 = new QTableWidgetItem(desc);
    auto *item3 = new QTableWidgetItem(data + (data.isEmpty() ? "" : " | ") + status);

    // 类型颜色
    if (type.startsWith("SDO")) {
        item1->setForeground(QColor(0x21, 0x96, 0xF3)); // blue
    } else if (type == "NMT") {
        item1->setForeground(QColor(0xFF, 0x98, 0x00)); // orange
    }

    // 状态颜色
    if (status.contains("成功")) {
        item3->setForeground(QColor(0x4C, 0xAF, 0x50)); // green
    } else if (status.contains("错误") || status.contains("失败")) {
        item3->setForeground(QColor(0xF4, 0x43, 0x36)); // red
    }

    m_logTable->setItem(row, 0, item0);
    m_logTable->setItem(row, 1, item1);
    m_logTable->setItem(row, 2, item2);
    m_logTable->setItem(row, 3, item3);

    m_logTable->scrollToBottom();
}
