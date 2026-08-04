#ifndef DEVICECONNECTIONTAB_H
#define DEVICECONNECTIONTAB_H

#include <QWidget>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QGroupBox;
class CanSimulator;
class CanDeviceManager;

/**
 * @brief 设备连接标签页 — 中央区域
 *
 * 从侧边栏 DevicePanel 点击设备条目后打开，
 * 包含完整的设备参数配置（通道、波特率、CAN FD、高级时序等），
 * 以及连接/断开控制。
 *
 * 支持不同设备类型的参数差异（模拟器 / ZLG / PEAK …）。
 */
class DeviceConnectionTab : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceConnectionTab(QWidget *parent = nullptr);

    void setSimulator(CanSimulator *sim);
    void setDeviceManager(CanDeviceManager *mgr);

    /// 设置要配置的设备（由侧边栏点击触发）
    /// @param deviceKind 0=模拟器, 1=ZLG
    /// @param devIndex 设备序号
    /// @param deviceName 设备显示名称
    void setDevice(int deviceKind, int devIndex, const QString &deviceName);

signals:
    void deviceConnectRequested(const QString &device, int baudrate);
    void deviceDisconnectRequested();
    /// 设备连接请求（携带完整参数）
    /// @param deviceKind 0=模拟器, 1=ZLG
    /// @param devIndex 设备序号
    /// @param channel 通道号 (0-based)
    /// @param arbBaud 仲裁段波特率
    /// @param dataBaud 数据段波特率
    /// @param canFd CAN FD 模式
    void deviceConnectRequestedV2(int deviceKind, int devIndex, int channel,
                                  int arbBaud, int dataBaud, bool canFd);

private slots:
    void onConnect();
    void onDisconnect();
    void onCanFdToggled(bool enabled);

private:
    // 设备信息
    QLabel *m_deviceLabel;
    int m_deviceKind = 0;
    int m_devIndex = 0;
    QString m_deviceName;

    // 基本配置
    QComboBox *m_channelCombo;
    QComboBox *m_fdCombo;

    // 波特率配置
    QComboBox *m_baudCombo;
    QComboBox *m_dataBaudCombo;
    QGroupBox *m_dataBaudGroup;

    // 高级参数
    QSpinBox *m_samplePointSpin;
    QSpinBox *m_sjwSpin;
    QSpinBox *m_tseg1Spin;
    QSpinBox *m_tseg2Spin;

    // 连接控制
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QLabel *m_statusLabel;

    CanSimulator *m_simulator = nullptr;
    CanDeviceManager *m_deviceMgr = nullptr;

    void setupUi();
    void updateCanFdVisibility();
};

#endif // DEVICECONNECTIONTAB_H
