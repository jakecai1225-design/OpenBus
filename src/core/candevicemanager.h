#ifndef CANDEVICEMANAGER_H
#define CANDEVICEMANAGER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <atomic>
#include <memory>
#include "core/canframe.h"
#include "core/candevice.h"
#include "utils/message_queue.h"

class CanSimulator;
class CanDeviceZLG;

/**
 * @brief CAN 设备管理器 — QObject 桥接层
 *
 * 统一管理 CanSimulator（内置模拟器）和 ICanDevice（真实硬件），
 * 对外暴露与 CanSimulator 相同的信号接口（frameGenerated），
 * 实现 MainWindow 中的无缝替换。
 *
 * 架构：CanDeviceManager → (CanSimulator | ICanDevice) → frameGenerated → MainWindow
 */
class CanDeviceManager : public QObject
{
    Q_OBJECT

public:
    /// 设备类型（映射到 ICanDevice::Brand）
    enum class DeviceKind {
        Simulator,  ///< 内置模拟器（无硬件）
        ZLG,        ///< ZLG 致远电子
        PEAK,       ///< PEAK PCAN
        Kvaser,     ///< Kvaser canlib32
        TongXing,   ///< 同星科技
        SLCAN,      ///< 开源 SLCAN / serial-CAN
        Candle,     ///< Candle / GS_USB 开源 USB CAN (CANable 等)
    };

    explicit CanDeviceManager(QObject *parent = nullptr);
    ~CanDeviceManager();

    // ---- 配置 ----

    /// 设置设备类型和参数（在 start() 前调用）
    /// @param kind 设备类型
    /// @param devIndex 设备序号（0-based）
    /// @param channel 通道号（0-based）
    /// @param arbBaud 仲裁段波特率
    /// @param dataBaud 数据段波特率
    /// @param canFd CAN FD 模式
    /// @param subType 厂商设备子类型（如 ZLG DEV_USBCANFD_200U=41, PEAK PCAN_USBFD=0x54）
    void configure(DeviceKind kind, int devIndex, int channel,
                   int arbBaud, int dataBaud, bool canFd, int subType = 0);

    /// 快捷：配置为模拟器模式
    void configureSimulator(int channel, int intervalMs, int baudrate);

    // ---- 运行控制（与 CanSimulator 同接口）----

    bool isRunning() const { return m_running; }
    void start();
    void stop();

    // ---- 状态查询 ----

    DeviceKind currentKind() const { return m_kind; }
    bool isRealDevice() const { return m_kind != DeviceKind::Simulator; }
    QString currentDeviceName() const;

    /// 队列中待消费帧数
    size_t pendingFrames() const { return m_queue.approxSize(); }

    // ---- 发送 ----

    /// 通过当前设备发送一帧（模拟器模式下无效）
    /// @param frame 待发送帧
    /// @param echo 可选出参：发送成功时写入带 Tx 标记与时间戳的回环帧
    ///        （时间基准与接收帧归一化一致：steady_clock - 启动时钟），
    ///        供调用方推回显示链路（Trace/Graphic 可见 Tx 帧，对齐 CANoe）
    bool sendFrame(const CanFrame &frame, CanFrame *echo = nullptr);

    // ---- 硬件接收滤波器 ----

    /// 设置硬件接收滤波器（仅真实设备模式有效）
    /// @param code 滤波码
    /// @param mask 滤波掩码
    /// @param extended 是否扩展帧
    /// @return true 成功
    bool setAcceptanceFilter(quint32 code, quint32 mask, bool extended);

    /// 清除硬件接收滤波器（接收所有帧）
    bool clearAcceptanceFilter();

    // ---- 设备枚举 ----

    /// 枚举所有可用硬件设备
    static QStringList enumerateDevices();

public slots:
    void drainQueue();

signals:
    /// 帧到达（与 CanSimulator::frameGenerated 同签名）
    void frameGenerated(const CanFrame &frame);

    /// 设备连接状态变化
    void connectionChanged(bool connected, const QString &deviceName);

    /// 错误信息
    void errorOccurred(const QString &message);

private:
    std::atomic<bool> m_running{false};

    // ---- 设备配置 ----
    DeviceKind m_kind = DeviceKind::Simulator;
    int m_devIndex = 0;
    int m_channel = 0;
    int m_arbBaud = 500000;
    int m_dataBaud = 2000000;
    bool m_canFd = false;
    int m_devSubType = 0;           ///< 厂商设备子类型

    // ---- 模拟器模式 ----
    CanSimulator *m_simulator = nullptr;  ///< 内置模拟器（非拥有，由 MainWindow 持有）

    // ---- 真实设备模式 ----
    std::unique_ptr<ICanDevice> m_device;  ///< 真实硬件设备实例

    // ---- 帧转发管线 ----
    QTimer m_drainTimer;          ///< 主线程批量消费定时器
    QThread *m_recvThread = nullptr;  ///< 接收线程（真实设备模式）
    FrameQueue m_queue;           ///< 无锁队列：recv thread → main
    std::chrono::steady_clock::time_point m_startClock;  ///< 接收起始时钟

    /// DeviceKind → ICanDevice::Brand 映射
    static ICanDevice::Brand kindToBrand(DeviceKind k);

    /// 接收线程主循环（含时间戳归一化）
    void recvLoop();
};

#endif // CANDEVICEMANAGER_H
