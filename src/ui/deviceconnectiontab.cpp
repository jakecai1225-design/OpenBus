#include "deviceconnectiontab.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFrame>
#include <QMessageBox>

DeviceConnectionTab::DeviceConnectionTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    populateTimingPresets();
}

void DeviceConnectionTab::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // ---- 设备标题 ----
    m_deviceLabel = new QLabel(QStringLiteral("设备: 未选择"), this);
    m_deviceLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 4px;");
    mainLayout->addWidget(m_deviceLabel);

    auto *sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(sep1);

    // ---- 基本配置 ----
    auto *basicGroup = new QGroupBox(QStringLiteral("基本配置"), this);
    auto *basicLayout = new QGridLayout(basicGroup);
    basicLayout->setSpacing(6);

    basicLayout->addWidget(new QLabel(QStringLiteral("CAN 模式:"), basicGroup), 0, 0);
    m_fdCombo = new QComboBox(basicGroup);
    m_fdCombo->addItem(QStringLiteral("CAN 2.0A (Classic)"));
    m_fdCombo->addItem(QStringLiteral("CAN FD (ISO 11898-1)"));
    basicLayout->addWidget(m_fdCombo, 0, 1);

    basicLayout->addWidget(new QLabel(QStringLiteral("通道使能:"), basicGroup), 1, 0);
    auto *chLayout = new QHBoxLayout;
    auto *ch1 = new QCheckBox(QStringLiteral("通道 1"), basicGroup);
    ch1->setChecked(true);
    auto *ch2 = new QCheckBox(QStringLiteral("通道 2"), basicGroup);
    m_channelChecks << ch1 << ch2;
    chLayout->addWidget(ch1);
    chLayout->addWidget(ch2);
    chLayout->addStretch();
    basicLayout->addLayout(chLayout, 1, 1);

    mainLayout->addWidget(basicGroup);

    // ---- 仲裁段配置 ----
    auto *arbGroup = new QGroupBox(QStringLiteral("仲裁段配置"), this);
    auto *arbLayout = new QGridLayout(arbGroup);
    arbLayout->setSpacing(6);

    arbLayout->addWidget(new QLabel(QStringLiteral("波特率:"), arbGroup), 0, 0);
    m_baudCombo = new QComboBox(arbGroup);
    m_baudCombo->addItem("1000000");
    m_baudCombo->addItem("800000");
    m_baudCombo->addItem("500000");
    m_baudCombo->addItem("250000");
    m_baudCombo->addItem("125000");
    m_baudCombo->addItem("100000");
    m_baudCombo->addItem("50000");
    m_baudCombo->addItem("33333");
    m_baudCombo->setCurrentText("500000");
    arbLayout->addWidget(m_baudCombo, 0, 1);

    arbLayout->addWidget(new QLabel(QStringLiteral("时序预设:"), arbGroup), 1, 0);
    m_arbTimingCombo = new QComboBox(arbGroup);
    arbLayout->addWidget(m_arbTimingCombo, 1, 1);

    m_arbTimingDetail = new QLabel(arbGroup);
    m_arbTimingDetail->setStyleSheet("color: gray; font-size: 11px; padding-left: 8px;");
    arbLayout->addWidget(m_arbTimingDetail, 2, 0, 1, 2);

    mainLayout->addWidget(arbGroup);

    // ---- 数据段配置 (CAN FD) ----
    m_dataBaudGroup = new QGroupBox(QStringLiteral("数据段配置 (CAN FD)"), this);
    auto *dataLayout = new QGridLayout(m_dataBaudGroup);
    dataLayout->setSpacing(6);

    dataLayout->addWidget(new QLabel(QStringLiteral("数据波特率:"), m_dataBaudGroup), 0, 0);
    m_dataBaudCombo = new QComboBox(m_dataBaudGroup);
    m_dataBaudCombo->addItem("1000000");
    m_dataBaudCombo->addItem("2000000");
    m_dataBaudCombo->addItem("4000000");
    m_dataBaudCombo->addItem("5000000");
    m_dataBaudCombo->addItem("8000000");
    m_dataBaudCombo->setCurrentText("2000000");
    dataLayout->addWidget(m_dataBaudCombo, 0, 1);

    dataLayout->addWidget(new QLabel(QStringLiteral("时序预设:"), m_dataBaudGroup), 1, 0);
    m_dataTimingCombo = new QComboBox(m_dataBaudGroup);
    dataLayout->addWidget(m_dataTimingCombo, 1, 1);

    m_dataTimingDetail = new QLabel(m_dataBaudGroup);
    m_dataTimingDetail->setStyleSheet("color: gray; font-size: 11px; padding-left: 8px;");
    dataLayout->addWidget(m_dataTimingDetail, 2, 0, 1, 2);

    mainLayout->addWidget(m_dataBaudGroup);

    mainLayout->addStretch();

    // ---- 连接控制 ----
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);
    m_connectBtn = new QPushButton(QStringLiteral("连接"), this);
    m_connectBtn->setMinimumWidth(120);
    m_disconnectBtn = new QPushButton(QStringLiteral("断开"), this);
    m_disconnectBtn->setMinimumWidth(120);
    m_disconnectBtn->setEnabled(false);
    btnLayout->addWidget(m_connectBtn);
    btnLayout->addWidget(m_disconnectBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    m_statusLabel = new QLabel(QStringLiteral("● 未连接"), this);
    m_statusLabel->setStyleSheet("color: gray; font-size: 12px; padding: 4px;");
    mainLayout->addWidget(m_statusLabel);

    // ---- 信号 ----
    connect(m_connectBtn, &QPushButton::clicked, this, &DeviceConnectionTab::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DeviceConnectionTab::onDisconnect);
    connect(m_fdCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { onCanFdToggled(idx == 1); });
    connect(m_arbTimingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceConnectionTab::onArbTimingChanged);
    connect(m_dataTimingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeviceConnectionTab::onDataTimingChanged);

    updateCanFdVisibility();
}

void DeviceConnectionTab::populateTimingPresets()
{
    // 仲裁段时序预设
    m_arbPresets = {
        {QStringLiteral("CiA 推荐 — 87.5%"),  1, 13, 2, 87},
        {QStringLiteral("标准 — 80%"),        1, 15, 3, 80},
        {QStringLiteral("标准 — 75%"),        1, 12, 3, 75},
        {QStringLiteral("高速 — 75% (SJW=2)"), 2, 12, 3, 75},
        {QStringLiteral("保守 — 62.5%"),      1, 10, 5, 62},
    };

    for (int i = 0; i < m_arbPresets.size(); ++i) {
        m_arbTimingCombo->addItem(m_arbPresets[i].name);
        m_arbTimingCombo->setItemData(i, i);
    }
    m_arbTimingCombo->setCurrentIndex(0);
    onArbTimingChanged(0);

    // 数据段时序预设
    m_dataPresets = {
        {QStringLiteral("CiA 推荐 — 80%"),   1, 15, 3, 80},
        {QStringLiteral("标准 — 75%"),       1, 12, 3, 75},
        {QStringLiteral("高速 — 75% (SJW=2)"), 2, 12, 3, 75},
        {QStringLiteral("保守 — 62.5%"),     1, 10, 5, 62},
    };

    for (int i = 0; i < m_dataPresets.size(); ++i) {
        m_dataTimingCombo->addItem(m_dataPresets[i].name);
        m_dataTimingCombo->setItemData(i, i);
    }
    m_dataTimingCombo->setCurrentIndex(0);
    onDataTimingChanged(0);
}

QString DeviceConnectionTab::timingDetailText(const TimingPreset &p)
{
    return QStringLiteral("SJW=%1, TSEG1=%2, TSEG2=%3 (采样点 %4%, 总 %5 TQ)")
        .arg(p.sjw).arg(p.tseg1).arg(p.tseg2)
        .arg(p.samplePoint).arg(1 + p.tseg1 + p.tseg2);
}

void DeviceConnectionTab::setSimulator(CanSimulator *sim)
{
    m_simulator = sim;
}

void DeviceConnectionTab::setDeviceManager(CanDeviceManager *mgr)
{
    m_deviceMgr = mgr;
}

void DeviceConnectionTab::setDevice(int deviceKind, int devIndex, const QString &deviceName)
{
    m_deviceKind = deviceKind;
    m_devIndex = devIndex;
    m_deviceName = deviceName;

    m_deviceLabel->setText(QStringLiteral("设备: %1").arg(deviceName));

    // 未实现的设备类型
    bool implemented = (deviceKind <= 1);
    m_connectBtn->setEnabled(implemented);
    if (!implemented) {
        m_connectBtn->setText(QStringLiteral("连接 (待实现)"));
        m_statusLabel->setText(QStringLiteral("● 该设备类型暂未实现"));
        m_statusLabel->setStyleSheet("color: orange; font-size: 12px; padding: 4px;");
    } else {
        m_connectBtn->setText(QStringLiteral("连接"));
        m_statusLabel->setText(QStringLiteral("● 未连接"));
        m_statusLabel->setStyleSheet("color: gray; font-size: 12px; padding: 4px;");
    }

    // 重置按钮状态
    m_disconnectBtn->setEnabled(false);

    updateCanFdVisibility();
}

void DeviceConnectionTab::onConnect()
{
    // 获取选中的通道列表
    QList<int> channels;
    for (int i = 0; i < m_channelChecks.size(); ++i) {
        if (m_channelChecks[i]->isChecked())
            channels << i;
    }
    if (channels.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("设备连接"),
                              QStringLiteral("请至少选择一个通道"));
        return;
    }

    int channel = channels.first(); // 0-based
    int baudrate = m_baudCombo->currentText().toInt();
    bool canFd = (m_fdCombo->currentIndex() == 1);
    int dataBaud = canFd ? m_dataBaudCombo->currentText().toInt() : 0;

    // 获取时序预设
    int arbIdx = m_arbTimingCombo->currentIndex();
    if (arbIdx >= 0 && arbIdx < m_arbPresets.size()) {
        // 时序参数已通过预设选定，后续可传递给设备
        // const auto &tp = m_arbPresets[arbIdx];
    }

    if (m_deviceKind == 0) {
        // 模拟器模式
        if (m_simulator) {
            m_simulator->setChannel(static_cast<quint8>(channel + 1));
            m_simulator->setBaudrate(baudrate);
            m_simulator->start();
        }
    } else if (m_deviceKind == 1) {
        // ZLG 真实设备
        emit deviceConnectRequestedV2(m_deviceKind, m_devIndex,
                                       channel, baudrate, dataBaud, canFd);
    } else {
        // 未实现的设备类型
        QMessageBox::information(this, QStringLiteral("设备连接"),
                                  QStringLiteral("该设备类型 (%1) 暂未实现").arg(m_deviceName));
        return;
    }

    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);

    QStringList chStrs;
    for (int ch : channels)
        chStrs << QStringLiteral("Ch%1").arg(ch + 1);
    m_statusLabel->setText(QStringLiteral("● 已连接: %1 (%2, %3%4)")
        .arg(m_deviceName)
        .arg(chStrs.join(", "))
        .arg(baudrate)
        .arg(canFd ? QStringLiteral("/D%1").arg(dataBaud) : QString()));
    m_statusLabel->setStyleSheet("color: green; font-size: 12px; padding: 4px;");

    emit deviceConnectRequested(m_deviceName, baudrate);
}

void DeviceConnectionTab::onDisconnect()
{
    if (m_deviceKind == 0) {
        if (m_simulator)
            m_simulator->stop();
    }
    // 真实设备的停止由 MainWindow 在 deviceDisconnectRequested 信号中处理

    m_connectBtn->setEnabled(m_deviceKind <= 1);
    m_disconnectBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("● 未连接"));
    m_statusLabel->setStyleSheet("color: gray; font-size: 12px; padding: 4px;");

    emit deviceDisconnectRequested();
}

void DeviceConnectionTab::onCanFdToggled(bool enabled)
{
    Q_UNUSED(enabled)
    updateCanFdVisibility();
}

void DeviceConnectionTab::onArbTimingChanged(int index)
{
    if (index >= 0 && index < m_arbPresets.size())
        m_arbTimingDetail->setText(timingDetailText(m_arbPresets[index]));
}

void DeviceConnectionTab::onDataTimingChanged(int index)
{
    if (index >= 0 && index < m_dataPresets.size())
        m_dataTimingDetail->setText(timingDetailText(m_dataPresets[index]));
}

void DeviceConnectionTab::updateCanFdVisibility()
{
    bool isSim = (m_deviceKind == 0);
    bool isFd = (m_fdCombo->currentIndex() == 1);
    m_dataBaudGroup->setVisible(!isSim && isFd);
}
