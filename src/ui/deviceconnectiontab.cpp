#include "deviceconnectiontab.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QFrame>

DeviceConnectionTab::DeviceConnectionTab(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
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

    basicLayout->addWidget(new QLabel(QStringLiteral("通道:"), basicGroup), 0, 0);
    m_channelCombo = new QComboBox(basicGroup);
    m_channelCombo->addItem("1");
    m_channelCombo->addItem("2");
    basicLayout->addWidget(m_channelCombo, 0, 1);

    basicLayout->addWidget(new QLabel(QStringLiteral("CAN 模式:"), basicGroup), 1, 0);
    m_fdCombo = new QComboBox(basicGroup);
    m_fdCombo->addItem(QStringLiteral("CAN 2.0A (Classic)"));
    m_fdCombo->addItem(QStringLiteral("CAN FD (ISO 11898-1)"));
    basicLayout->addWidget(m_fdCombo, 1, 1);

    mainLayout->addWidget(basicGroup);

    // ---- 仲裁段波特率 ----
    auto *arbGroup = new QGroupBox(QStringLiteral("仲裁段波特率"), this);
    auto *arbLayout = new QGridLayout(arbGroup);
    arbLayout->setSpacing(6);

    arbLayout->addWidget(new QLabel(QStringLiteral("波特率:"), arbGroup), 0, 0);
    m_baudCombo = new QComboBox(arbGroup);
    m_baudCombo->addItem("500000");
    m_baudCombo->addItem("250000");
    m_baudCombo->addItem("1000000");
    m_baudCombo->addItem("125000");
    m_baudCombo->addItem("800000");
    m_baudCombo->addItem("33333");
    m_baudCombo->addItem("50000");
    m_baudCombo->addItem("100000");
    m_baudCombo->setCurrentText("500000");
    arbLayout->addWidget(m_baudCombo, 0, 1);

    mainLayout->addWidget(arbGroup);

    // ---- 数据段波特率 (CAN FD) ----
    m_dataBaudGroup = new QGroupBox(QStringLiteral("数据段波特率 (CAN FD)"), this);
    auto *dataLayout = new QGridLayout(m_dataBaudGroup);
    dataLayout->setSpacing(6);

    dataLayout->addWidget(new QLabel(QStringLiteral("数据波特率:"), m_dataBaudGroup), 0, 0);
    m_dataBaudCombo = new QComboBox(m_dataBaudGroup);
    m_dataBaudCombo->addItem("2000000");
    m_dataBaudCombo->addItem("1000000");
    m_dataBaudCombo->addItem("4000000");
    m_dataBaudCombo->addItem("5000000");
    m_dataBaudCombo->addItem("8000000");
    m_dataBaudCombo->setCurrentText("2000000");
    dataLayout->addWidget(m_dataBaudCombo, 0, 1);

    mainLayout->addWidget(m_dataBaudGroup);

    // ---- 高级参数 ----
    auto *advGroup = new QGroupBox(QStringLiteral("高级时序参数"), this);
    auto *advLayout = new QGridLayout(advGroup);
    advLayout->setSpacing(6);

    advLayout->addWidget(new QLabel(QStringLiteral("采样点 (%):"), advGroup), 0, 0);
    m_samplePointSpin = new QSpinBox(advGroup);
    m_samplePointSpin->setRange(25, 90);
    m_samplePointSpin->setValue(75);
    advLayout->addWidget(m_samplePointSpin, 0, 1);

    advLayout->addWidget(new QLabel(QStringLiteral("SJW (TQ):"), advGroup), 1, 0);
    m_sjwSpin = new QSpinBox(advGroup);
    m_sjwSpin->setRange(1, 4);
    m_sjwSpin->setValue(1);
    advLayout->addWidget(m_sjwSpin, 1, 1);

    advLayout->addWidget(new QLabel(QStringLiteral("TSEG1 (TQ):"), advGroup), 2, 0);
    m_tseg1Spin = new QSpinBox(advGroup);
    m_tseg1Spin->setRange(1, 16);
    m_tseg1Spin->setValue(12);
    advLayout->addWidget(m_tseg1Spin, 2, 1);

    advLayout->addWidget(new QLabel(QStringLiteral("TSEG2 (TQ):"), advGroup), 3, 0);
    m_tseg2Spin = new QSpinBox(advGroup);
    m_tseg2Spin->setRange(1, 8);
    m_tseg2Spin->setValue(3);
    advLayout->addWidget(m_tseg2Spin, 3, 1);

    mainLayout->addWidget(advGroup);

    mainLayout->addStretch();

    // ---- 连接控制 ----
    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);
    m_connectBtn = new QPushButton(QStringLiteral("\xF0\x9F\x94\x97 连接"), this);
    m_connectBtn->setMinimumWidth(120);
    m_disconnectBtn = new QPushButton(QStringLiteral("\xE2\x8C\x96 断开"), this);
    m_disconnectBtn->setMinimumWidth(120);
    m_disconnectBtn->setEnabled(false);
    btnLayout->addWidget(m_connectBtn);
    btnLayout->addWidget(m_disconnectBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    m_statusLabel = new QLabel(QStringLiteral("\xE2\x97\x8F 未连接"), this);
    m_statusLabel->setStyleSheet("color: gray; font-size: 12px; padding: 4px;");
    mainLayout->addWidget(m_statusLabel);

    // ---- 信号 ----
    connect(m_connectBtn, &QPushButton::clicked, this, &DeviceConnectionTab::onConnect);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &DeviceConnectionTab::onDisconnect);
    connect(m_fdCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) { onCanFdToggled(idx == 1); });

    updateCanFdVisibility();
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

    // 模拟器模式隐藏部分配置
    bool isSim = (deviceKind == 0);
    m_dataBaudGroup->setVisible(!isSim && m_fdCombo->currentIndex() == 1);

    // 重置状态
    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("\xE2\x97\x8F 未连接"));
    m_statusLabel->setStyleSheet("color: gray; font-size: 12px; padding: 4px;");
}

void DeviceConnectionTab::onConnect()
{
    int channel = m_channelCombo->currentText().toInt() - 1; // 0-based
    int baudrate = m_baudCombo->currentText().toInt();
    bool canFd = (m_fdCombo->currentIndex() == 1);
    int dataBaud = canFd ? m_dataBaudCombo->currentText().toInt() : 0;

    if (m_deviceKind == 0) {
        // 模拟器模式
        if (m_simulator) {
            m_simulator->setChannel(static_cast<quint8>(channel + 1));
            m_simulator->setBaudrate(baudrate);
            m_simulator->start();
        }
    } else {
        // 真实设备模式
        emit deviceConnectRequestedV2(m_deviceKind, m_devIndex,
                                       channel, baudrate, dataBaud, canFd);
    }

    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("\xE2\x97\x8F 已连接: %1 (Ch%2, %3)")
        .arg(m_deviceName).arg(channel + 1).arg(baudrate));
    m_statusLabel->setStyleSheet("color: green; font-size: 12px; padding: 4px;");

    emit deviceConnectRequested(m_deviceName, baudrate);
}

void DeviceConnectionTab::onDisconnect()
{
    if (m_deviceKind == 0) {
        if (m_simulator)
            m_simulator->stop();
    } else {
        emit deviceDisconnectRequested();
    }

    m_connectBtn->setEnabled(true);
    m_disconnectBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("\xE2\x97\x8F 未连接"));
    m_statusLabel->setStyleSheet("color: gray; font-size: 12px; padding: 4px;");

    emit deviceDisconnectRequested();
}

void DeviceConnectionTab::onCanFdToggled(bool enabled)
{
    Q_UNUSED(enabled)
    updateCanFdVisibility();
}

void DeviceConnectionTab::updateCanFdVisibility()
{
    bool isSim = (m_deviceKind == 0);
    bool isFd = (m_fdCombo->currentIndex() == 1);
    m_dataBaudGroup->setVisible(!isSim && isFd);
}
