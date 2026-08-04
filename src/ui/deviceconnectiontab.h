#ifndef DEVICECONNECTIONTAB_H
#define DEVICECONNECTIONTAB_H

#include <QWidget>
#include <QList>

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
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
 * 不同设备共用此界面，通过 setDevice() 切换设备类型。
 */
class DeviceConnectionTab : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceConnectionTab(QWidget *parent = nullptr);

    void setSimulator(CanSimulator *sim);
    void setDeviceManager(CanDeviceManager *mgr);

    /// 设置要配置的设备（由侧边栏点击触发）
    /// @param deviceKind 0=模拟器, 1=ZLG, 2=PEAK, 3=Kvaser, 4=CandleLight
    /// @param devIndex 设备序号
    /// @param deviceName 设备显示名称
    void setDevice(int deviceKind, int devIndex, const QString &deviceName);

signals:
    void deviceConnectRequested(const QString &device, int baudrate);
    void deviceDisconnectRequested();
    /// 设备连接请求（携带完整参数）
    void deviceConnectRequestedV2(int deviceKind, int devIndex, int channel,
                                  int arbBaud, int dataBaud, bool canFd);

private slots:
    void onConnect();
    void onDisconnect();
    void onCanFdToggled(bool enabled);
    void onArbTimingChanged(int index);
    void onDataTimingChanged(int index);

private:
    // 时序预设
    struct TimingPreset {
        QString name;
        int sjw;
        int tseg1;
        int tseg2;
        int samplePoint;
    };
    QList<TimingPreset> m_arbPresets;
    QList<TimingPreset> m_dataPresets;

    // 设备信息
    QLabel *m_deviceLabel;
    int m_deviceKind = 0;
    int m_devIndex = 0;
    QString m_deviceName;

    // 基本配置
    QList<QCheckBox *> m_channelChecks;
    QComboBox *m_fdCombo;

    // 仲裁段波特率
    QComboBox *m_baudCombo;
    QComboBox *m_arbTimingCombo;
    QLabel *m_arbTimingDetail;

    // 数据段波特率 (CAN FD)
    QGroupBox *m_dataBaudGroup;
    QComboBox *m_dataBaudCombo;
    QComboBox *m_dataTimingCombo;
    QLabel *m_dataTimingDetail;

    // 连接控制
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QLabel *m_statusLabel;

    CanSimulator *m_simulator = nullptr;
    CanDeviceManager *m_deviceMgr = nullptr;

    void setupUi();
    void updateCanFdVisibility();
    void populateTimingPresets();
    QString timingDetailText(const TimingPreset &p);
};

#endif // DEVICECONNECTIONTAB_H
