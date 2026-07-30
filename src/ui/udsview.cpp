#include "udsview.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QHeaderView>
#include <QDateTime>
#include <QSplitter>

// ============================================================
//  UDS 服务定义
// ============================================================
struct UdsService {
    int id;
    const char *name;
};

static const UdsService s_services[] = {
    {0x10, "0x10 DiagnosticSessionControl"},
    {0x11, "0x11 ECUReset"},
    {0x14, "0x14 ClearDiagnosticInformation"},
    {0x19, "0x19 ReadDTCInformation"},
    {0x22, "0x22 ReadDataByIdentifier"},
    {0x24, "0x24 ReadScalingDataByIdentifier"},
    {0x27, "0x27 SecurityAccess"},
    {0x28, "0x28 CommunicationControl"},
    {0x2A, "0x2A ReadDataByPeriodicIdentifier"},
    {0x2C, "0x2C DynamicallyDefineDataIdentifier"},
    {0x2E, "0x2E WriteDataByIdentifier"},
    {0x31, "0x31 RoutineControl"},
    {0x34, "0x34 RequestDownload"},
    {0x35, "0x35 RequestUpload"},
    {0x36, "0x36 TransferData"},
    {0x37, "0x37 RequestTransferExit"},
    {0x3D, "0x3D WriteMemoryByAddress"},
    {0x3E, "0x3E TesterPresent"},
    {0x83, "0x83 AccessTimingParameter"},
    {0x85, "0x85 ControlDTCSetting"},
    {0x86, "0x86 ResponseOnEvent"},
    {0x87, "0x87 ResponseOnEvent"},
};

// ============================================================
//  NRC 定义
// ============================================================
struct UdsNrc {
    int code;
    const char *name;
};

static const UdsNrc s_nrcs[] = {
    {0x10, "GeneralReject"},
    {0x11, "ServiceNotSupported"},
    {0x12, "SubFunctionNotSupported"},
    {0x13, "IncorrectMessageLengthOrInvalidFormat"},
    {0x14, "ResponseTooLong"},
    {0x21, "RepeatRequest"},
    {0x22, "GeneralProgrammingFailure"},
    {0x24, "RequestSequenceError"},
    {0x26, "FailurePreventsExecutionOfRequestedAction"},
    {0x31, "RequestOutOfRange"},
    {0x33, "SecurityAccessDenied"},
    {0x35, "InvalidKey"},
    {0x36, "ExceededNumberOfAttempts"},
    {0x37, "RequiredTimeDelayNotExpired"},
    {0x70, "UploadDownloadNotAccepted"},
    {0x71, "TransferDataSuspended"},
    {0x72, "GeneralProgrammingFailure"},
    {0x73, "WrongBlockSequenceCounter"},
    {0x78, "RequestCorrectlyReceivedResponsePending"},
    {0x7E, "SubFunctionNotSupportedInActiveSession"},
    {0x7F, "ServiceNotSupportedInActiveSession"},
    {0x82, "GeneralProgrammingFailure"},
};

// ============================================================
//  构造
// ============================================================

UdsView::UdsView(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void UdsView::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    mainLayout->addWidget(splitter);

    // ---- 上部：请求构建器 ----
    auto *reqGroup = new QGroupBox("诊断请求", this);
    auto *reqLayout = new QFormLayout(reqGroup);

    m_serviceCombo = new QComboBox(reqGroup);
    populateServices();
    reqLayout->addRow("服务:", m_serviceCombo);

    m_subFuncCombo = new QComboBox(reqGroup);
    reqLayout->addRow("子功能:", m_subFuncCombo);

    m_dataEdit = new QLineEdit(reqGroup);
    m_dataEdit->setPlaceholderText("22 F1 90  (空格分隔的 hex 字节)");
    reqLayout->addRow("数据:", m_dataEdit);

    // CAN ID 配置行
    auto *idLayout = new QHBoxLayout();
    m_reqIdSpin = new QSpinBox(reqGroup);
    m_reqIdSpin->setRange(0, 0x7FF);
    m_reqIdSpin->setValue(0x7E0);
    m_reqIdSpin->setDisplayIntegerBase(16);
    m_reqIdSpin->setPrefix("0x");
    idLayout->addWidget(new QLabel("请求 ID:"));
    idLayout->addWidget(m_reqIdSpin);

    m_rspIdSpin = new QSpinBox(reqGroup);
    m_rspIdSpin->setRange(0, 0x7FF);
    m_rspIdSpin->setValue(0x7E8);
    m_rspIdSpin->setDisplayIntegerBase(16);
    m_rspIdSpin->setPrefix("0x");
    idLayout->addWidget(new QLabel("响应 ID:"));
    idLayout->addWidget(m_rspIdSpin);

    m_autoRespCheck = new QCheckBox("模拟响应", reqGroup);
    m_autoRespCheck->setChecked(true);
    idLayout->addWidget(m_autoRespCheck);
    idLayout->addStretch();

    reqLayout->addRow("CAN ID:", idLayout);

    // 按钮行
    auto *btnLayout = new QHBoxLayout();
    m_sendBtn = new QPushButton("▶ 发送请求", reqGroup);
    m_sendBtn->setObjectName("primaryBtn");
    m_clearBtn = new QPushButton("🗑 清空日志", reqGroup);
    btnLayout->addWidget(m_sendBtn);
    btnLayout->addWidget(m_clearBtn);
    btnLayout->addStretch();
    reqLayout->addRow("", btnLayout);

    splitter->addWidget(reqGroup);

    // ---- 中部：事务日志 ----
    auto *logGroup = new QGroupBox("诊断事务日志", this);
    auto *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(2, 2, 2, 2);

    m_logTable = new QTableWidget(0, 7, logGroup);
    m_logTable->setHorizontalHeaderLabels({"时间", "方向", "服务", "子功能", "数据 (hex)", "状态", "NRC 描述"});
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    m_logTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_logTable->setColumnWidth(0, 100);
    m_logTable->setColumnWidth(1, 60);
    m_logTable->setColumnWidth(2, 200);
    m_logTable->setColumnWidth(3, 60);
    m_logTable->setColumnWidth(5, 80);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logLayout->addWidget(m_logTable);

    splitter->addWidget(logGroup);

    // ---- 下部：响应详情 ----
    auto *detailGroup = new QGroupBox("响应详情", this);
    auto *detailLayout = new QVBoxLayout(detailGroup);
    detailLayout->setContentsMargins(2, 2, 2, 2);
    m_detailEdit = new QTextEdit(detailGroup);
    m_detailEdit->setReadOnly(true);
    m_detailEdit->setMaximumHeight(120);
    m_detailEdit->setPlaceholderText("选择日志行查看详细解析...");
    detailLayout->addWidget(m_detailEdit);

    splitter->addWidget(detailGroup);

    // 比例
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);

    // ---- 信号连接 ----
    connect(m_serviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UdsView::onServiceChanged);
    connect(m_sendBtn, &QPushButton::clicked, this, &UdsView::onSendRequest);
    connect(m_clearBtn, &QPushButton::clicked, this, &UdsView::onClearLog);
    connect(m_logTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        int row = m_logTable->currentRow();
        if (row < 0) return;
        QString text = QString("服务: %1\n").arg(m_logTable->item(row, 2)->text());
        text += QString("方向: %1\n").arg(m_logTable->item(row, 1)->text());
        text += QString("数据: %1\n").arg(m_logTable->item(row, 4)->text());
        text += QString("状态: %1\n").arg(m_logTable->item(row, 5)->text());
        text += QString("NRC: %1").arg(m_logTable->item(row, 6)->text());
        m_detailEdit->setPlainText(text);
    });

    // 初始化子功能
    onServiceChanged(0);
}

// ============================================================
//  服务列表填充
// ============================================================

void UdsView::populateServices()
{
    for (const auto &svc : s_services) {
        m_serviceCombo->addItem(QString::fromUtf8(svc.name), svc.id);
    }
}

void UdsView::populateSubFunctions(int serviceId)
{
    m_subFuncCombo->clear();

    switch (serviceId) {
    case 0x10: // DiagnosticSessionControl
        m_subFuncCombo->addItem("0x01 Default Session", 0x01);
        m_subFuncCombo->addItem("0x02 Programming Session", 0x02);
        m_subFuncCombo->addItem("0x03 Extended Session", 0x03);
        m_subFuncCombo->addItem("0x04 Safety Session", 0x04);
        break;
    case 0x11: // ECUReset
        m_subFuncCombo->addItem("0x01 Hard Reset", 0x01);
        m_subFuncCombo->addItem("0x02 Key Off/On Reset", 0x02);
        m_subFuncCombo->addItem("0x03 Soft Reset", 0x03);
        m_subFuncCombo->addItem("0x04 Enable Rapid Power Shut Down", 0x04);
        m_subFuncCombo->addItem("0x05 Disable Rapid Power Shut Down", 0x05);
        break;
    case 0x14: // ClearDiagnosticInformation
        m_subFuncCombo->addItem("0xFF Clear All DTCs", 0xFF);
        break;
    case 0x19: // ReadDTCInformation
        m_subFuncCombo->addItem("0x01 ReportNumberOfDTCByStatusMask", 0x01);
        m_subFuncCombo->addItem("0x02 ReportDTCByStatusMask", 0x02);
        m_subFuncCombo->addItem("0x06 ReportDTCExtDataRecordByStatusMask", 0x06);
        m_subFuncCombo->addItem("0x0A ReportSupportedDTCs", 0x0A);
        break;
    case 0x27: // SecurityAccess
        m_subFuncCombo->addItem("0x01 Request Seed", 0x01);
        m_subFuncCombo->addItem("0x02 Send Key", 0x02);
        m_subFuncCombo->addItem("0x03 Request Seed (Ecu)", 0x03);
        m_subFuncCombo->addItem("0x04 Send Key (Ecu)", 0x04);
        break;
    case 0x28: // CommunicationControl
        m_subFuncCombo->addItem("0x00 Enable Rx/Tx", 0x00);
        m_subFuncCombo->addItem("0x01 Disable Rx/Tx", 0x01);
        m_subFuncCombo->addItem("0x02 Disable Rx Enable Tx", 0x02);
        m_subFuncCombo->addItem("0x03 Enable Rx Disable Tx", 0x03);
        break;
    case 0x31: // RoutineControl
        m_subFuncCombo->addItem("0x01 Start Routine", 0x01);
        m_subFuncCombo->addItem("0x02 Stop Routine", 0x02);
        m_subFuncCombo->addItem("0x03 Request Routine Result", 0x03);
        break;
    case 0x3E: // TesterPresent
        m_subFuncCombo->addItem("0x00 标准 (无响应)", 0x00);
        m_subFuncCombo->addItem("0x80 标准 (有响应)", 0x80);
        break;
    case 0x85: // ControlDTCSetting
        m_subFuncCombo->addItem("0x01 On (DTC 设置开启)", 0x01);
        m_subFuncCombo->addItem("0x02 Off (DTC 设置关闭)", 0x02);
        break;
    default:
        m_subFuncCombo->addItem("0x00 (默认)", 0x00);
        break;
    }
}

// ============================================================
//  事件处理
// ============================================================

void UdsView::onServiceChanged(int index)
{
    Q_UNUSED(index);
    int sid = m_serviceCombo->currentData().toInt();
    populateSubFunctions(sid);
}

void UdsView::onSendRequest()
{
    int sid = m_serviceCombo->currentData().toInt();
    int subFunc = m_subFuncCombo->currentData().toInt();
    QByteArray data = parseHex(m_dataEdit->text().trimmed());

    // 构建完整请求数据: SID + SubFunction + data
    QByteArray reqData;
    reqData.append(static_cast<char>(sid));
    reqData.append(static_cast<char>(subFunc));
    reqData.append(data);

    // 记录请求
    appendLog("请求", sid, subFunc, reqData, "已发送", "");

    // 模拟响应
    if (m_autoRespCheck->isChecked()) {
        onSimulateResponse();
    }
}

void UdsView::onSimulateResponse()
{
    int sid = m_serviceCombo->currentData().toInt();
    int subFunc = m_subFuncCombo->currentData().toInt();

    // 构建模拟正响应: SID+0x40 + SubFunction + data
    QByteArray rspData;
    rspData.append(static_cast<char>(sid + 0x40));
    rspData.append(static_cast<char>(subFunc));

    // 根据服务添加模拟数据
    switch (sid) {
    case 0x10: // DiagnosticSessionControl
        rspData.append(static_cast<char>(0x00));
        rspData.append(static_cast<char>(0x32));
        rspData.append(static_cast<char>(0x01));
        rspData.append(static_cast<char>(0xF4));
        break;
    case 0x11: // ECUReset
        // 无额外数据
        break;
    case 0x19: // ReadDTCInformation
        rspData.append(static_cast<char>(0xFF)); // DTCStatusAvailabilityMask
        rspData.append(static_cast<char>(0x00));
        rspData.append(static_cast<char>(0x02)); // 2 DTCs
        break;
    case 0x22: // ReadDataByIdentifier
        rspData.append(static_cast<char>(0x01));
        rspData.append(static_cast<char>(0x02));
        rspData.append(static_cast<char>(0x03));
        rspData.append(static_cast<char>(0x04));
        break;
    case 0x27: // SecurityAccess
        if (subFunc % 2 == 1) { // Request Seed
            rspData.append(static_cast<char>(0x12));
            rspData.append(static_cast<char>(0x34));
            rspData.append(static_cast<char>(0x56));
            rspData.append(static_cast<char>(0x78));
        }
        break;
    case 0x3E: // TesterPresent
        // 无额外数据
        break;
    default:
        break;
    }

    appendLog("响应", sid + 0x40, subFunc, rspData, "成功", "正响应");
}

void UdsView::onClearLog()
{
    m_logTable->setRowCount(0);
    m_detailEdit->clear();
}

// ============================================================
//  辅助方法
// ============================================================

void UdsView::appendLog(const QString &direction, int serviceId,
                        int subFunc, const QByteArray &data,
                        const QString &status, const QString &nrcDesc)
{
    int row = m_logTable->rowCount();
    m_logTable->insertRow(row);

    QString timeStr = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");

    auto *item0 = new QTableWidgetItem(timeStr);
    auto *item1 = new QTableWidgetItem(direction);
    auto *item2 = new QTableWidgetItem(serviceName(serviceId));
    auto *item3 = new QTableWidgetItem(QString("0x%1").arg(subFunc, 2, 16, QChar('0')).toUpper());
    auto *item4 = new QTableWidgetItem(bytesToHex(data));
    auto *item5 = new QTableWidgetItem(status);
    auto *item6 = new QTableWidgetItem(nrcDesc);

    // 方向颜色标记
    if (direction == "请求") {
        item1->setForeground(QColor(0x21, 0x96, 0xF3)); // blue
    } else {
        if (status == "成功")
            item1->setForeground(QColor(0x4C, 0xAF, 0x50)); // green
        else
            item1->setForeground(QColor(0xF4, 0x43, 0x36)); // red
    }

    m_logTable->setItem(row, 0, item0);
    m_logTable->setItem(row, 1, item1);
    m_logTable->setItem(row, 2, item2);
    m_logTable->setItem(row, 3, item3);
    m_logTable->setItem(row, 4, item4);
    m_logTable->setItem(row, 5, item5);
    m_logTable->setItem(row, 6, item6);

    m_logTable->scrollToBottom();
}

QString UdsView::serviceName(int sid)
{
    // 检查正响应 (SID + 0x40)
    int baseSid = (sid >= 0x50 && sid <= 0xC7) ? sid - 0x40 : sid;

    for (const auto &svc : s_services) {
        if (svc.id == baseSid) {
            QString name = QString::fromUtf8(svc.name);
            if (sid >= 0x50)
                name += " [正响应]";
            return name;
        }
    }

    // 检查负响应
    if (sid == 0x7F) {
        return "0x7F NegativeResponse";
    }

    return QString("0x%1").arg(sid, 2, 16, QChar('0')).toUpper();
}

QString UdsView::nrcName(int nrc)
{
    for (const auto &n : s_nrcs) {
        if (n.code == nrc)
            return QString("0x%1 %2").arg(nrc, 2, 16, QChar('0')).toUpper()
                       .arg(QString::fromUtf8(n.name));
    }
    return QString("0x%1 Unknown").arg(nrc, 2, 16, QChar('0')).toUpper();
}

QString UdsView::bytesToHex(const QByteArray &data, bool withSpace)
{
    QString result;
    for (int i = 0; i < data.size(); ++i) {
        if (i > 0 && withSpace)
            result += ' ';
        result += QString("%1").arg(static_cast<unsigned char>(data[i]), 2, 16, QChar('0')).toUpper();
    }
    return result;
}

QByteArray UdsView::parseHex(const QString &text)
{
    QByteArray result;
    QString cleaned = text;
    cleaned.remove(' ');
    cleaned.remove('\t');
    cleaned.remove('\n');
    cleaned.remove('\r');
    cleaned.remove(',');

    for (int i = 0; i + 1 < cleaned.length(); i += 2) {
        bool ok;
        unsigned char byte = cleaned.mid(i, 2).toUInt(&ok, 16);
        if (ok)
            result.append(static_cast<char>(byte));
    }
    return result;
}
