#include "candevicemanager.h"
#include "cansimulator.h"
#include "candevice_zlg.h"
#include "candevice_peak.h"
#include "logging.h"

#include <QThread>
#include <chrono>

// ============================================================
//  CanDeviceManager 实现
// ============================================================

CanDeviceManager::CanDeviceManager(QObject *parent)
    : QObject(parent)
{
    m_drainTimer.setTimerType(Qt::PreciseTimer);
    m_drainTimer.setInterval(2);  // 每 2ms 批量消费
    connect(&m_drainTimer, &QTimer::timeout, this, &CanDeviceManager::drainQueue);
}

CanDeviceManager::~CanDeviceManager()
{
    stop();
}

// ---- 配置 ----

void CanDeviceManager::configure(DeviceKind kind, int devIndex, int channel,
                                  int arbBaud, int dataBaud, bool canFd, int subType)
{
    // 如果正在运行且配置变了，先停止
    if (m_running)
        stop();

    m_kind = kind;
    m_devIndex = devIndex;
    m_channel = channel;
    m_arbBaud = arbBaud;
    m_dataBaud = dataBaud;
    m_canFd = canFd;
    m_devSubType = subType;
}

void CanDeviceManager::configureSimulator(int channel, int intervalMs, int baudrate)
{
    if (m_running)
        stop();
    m_kind = DeviceKind::Simulator;
    m_channel = channel;
    m_arbBaud = baudrate;
    // intervalMs 由 CanSimulator 自己管理
}

// ---- 运行控制 ----

void CanDeviceManager::start()
{
    if (m_running)
        return;

    if (m_kind == DeviceKind::Simulator) {
        // Simulator is owned/started by MainWindow; mark running for send echo.
        m_running = true;
        m_startClock = std::chrono::steady_clock::now();
        return;
    }

    // ---- 真实设备模式 — 使用工厂方法创建 ----

    ICanDevice::Brand brand = kindToBrand(m_kind);
    int subType = m_devSubType;

    // 子类型默认值：用户未指定时使用各品牌最常见的型号
    if (subType == 0) {
        switch (m_kind) {
        case DeviceKind::ZLG:  subType = CanDeviceZLG::DEV_USBCANFD_200U; break;
        case DeviceKind::PEAK: subType = CanDevicePEAK::PCAN_USBBUS1;     break; // classic PCAN-USB
        default: break;
        }
    }

    m_device = ICanDevice::create(brand, subType);
    if (!m_device) {
        emit errorOccurred(QStringLiteral("无法创建设备: %1 (DLL 缺失?)")
                           .arg(ICanDevice::brandName(brand)));
        return;
    }

    // 打开设备
    if (!m_device->open(m_devIndex, m_channel, m_arbBaud, m_dataBaud, m_canFd)) {
        emit errorOccurred(QStringLiteral("无法打开设备: %1")
                           .arg(m_device->deviceName()));
        m_device.reset();
        return;
    }

    m_running = true;
    m_startClock = std::chrono::steady_clock::now();

    // 启动接收线程
    m_recvThread = QThread::create([this]() { recvLoop(); });
    if (m_recvThread) {
        m_recvThread->start();
        OPENBUS_LOG_INFO("CanDeviceManager", "recv thread started: {}",
                     m_device->deviceName().toStdString());
    }

    // 启动主线程消费定时器
    m_drainTimer.start();

    emit connectionChanged(true, m_device->deviceName());
}

void CanDeviceManager::stop()
{
    if (!m_running)
        return;

    m_running = false;
    m_drainTimer.stop();

    // 等待接收线程退出
    if (m_recvThread) {
        m_recvThread->wait(1000);
        m_recvThread->deleteLater();
        m_recvThread = nullptr;
    }

    // 关闭真实设备
    if (m_device) {
        m_device->close();
        m_device.reset();
        emit connectionChanged(false, QString());
    }

    // 排空队列中残留帧
    drainQueue();
}

// ---- 帧消费 ----

void CanDeviceManager::drainQueue()
{
    QVector<CanFrame> frames;
    size_t got = m_queue.tryDequeueBulk(frames, 512);
    if (got == 0)
        return;

    emit framesGenerated(frames);
}

// ---- 状态查询 ----

QString CanDeviceManager::currentDeviceName() const
{
    if (m_kind == DeviceKind::Simulator)
        return QStringLiteral("模拟器 (内置)");
    if (m_device)
        return m_device->deviceName();
    return ICanDevice::brandName(kindToBrand(m_kind));
}

// ---- 发送 ----

bool CanDeviceManager::sendFrame(const CanFrame &frame, CanFrame *echo)
{
    auto fillTxEcho = [&]() {
        if (!echo)
            return;
        *echo = frame;
        echo->direction = CanFrame::Tx;
        // Match hardware Rx channel numbering (1-based) so Trace columns align.
        echo->channel = static_cast<quint8>(m_channel + 1);
        if (echo->timestampNs == 0) {
            const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - m_startClock).count();
            echo->timestampNs = static_cast<quint64>(nowNs);
        }
        echo->timestamp = static_cast<double>(echo->timestampNs) / 1e9;
    };

    // Simulator: local Tx loopback only (no physical bus). Lets plugins
    // (e.g. UDS) and Transceive feed Trace/Graphic/Flow via the shell hub.
    if (m_kind == DeviceKind::Simulator) {
        if (!m_running)
            return false;
        fillTxEcho();
        return true;
    }

    if (!m_running || !m_device)
        return false;

    if (m_device->send(frame) <= 0)
        return false;

    fillTxEcho();
    return true;
}

// ---- 硬件接收滤波器 ----

bool CanDeviceManager::setAcceptanceFilter(quint32 code, quint32 mask, bool extended)
{
    if (m_kind == DeviceKind::Simulator)
        return false;  // 模拟器不支持硬件滤波

    if (!m_device)
        return false;

    return m_device->setAcceptanceFilter(code, mask, extended);
}

bool CanDeviceManager::clearAcceptanceFilter()
{
    if (m_kind == DeviceKind::Simulator)
        return false;

    if (!m_device)
        return false;

    return m_device->clearAcceptanceFilter();
}

// ---- 设备枚举 ----

QStringList CanDeviceManager::enumerateDevices()
{
    QStringList list;
    list << QStringLiteral("模拟器 (内置)");

    // 统一枚举所有品牌设备
    auto devices = ICanDevice::enumerateAll();
    for (const auto &d : devices) {
        list << d.name;
    }

    return list;
}

// ---- DeviceKind → Brand 映射 ----

ICanDevice::Brand CanDeviceManager::kindToBrand(DeviceKind k)
{
    switch (k) {
    case DeviceKind::ZLG:      return ICanDevice::Brand::ZLG;
    case DeviceKind::PEAK:     return ICanDevice::Brand::PEAK;
    case DeviceKind::Kvaser:   return ICanDevice::Brand::Kvaser;
    case DeviceKind::TongXing: return ICanDevice::Brand::TongXing;
    case DeviceKind::SLCAN:    return ICanDevice::Brand::SLCAN;
    case DeviceKind::Candle:   return ICanDevice::Brand::Candle;
    case DeviceKind::Busmust:  return ICanDevice::Brand::Busmust;
    default:                   return ICanDevice::Brand::ZLG;
    }
}

// ---- 接收线程 ----

void CanDeviceManager::recvLoop()
{
    std::vector<CanFrame> recvBuf;

    while (m_running.load(std::memory_order_relaxed)) {
        // 从设备批量接收（10ms 超时）
        int got = m_device->recv(10, recvBuf);
        if (got > 0) {
            for (const auto &f : recvBuf) {
                CanFrame frame = f;

                // ---- 时间戳归一化 ----
                // 硬件未提供纳秒时间戳时，用 steady_clock 补零
                // 确保 ZLG/PEAK/Kvaser 混用时时间轴一致
                if (frame.timestampNs == 0) {
                    auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - m_startClock).count();
                    frame.timestampNs = static_cast<quint64>(nowNs);
                }
                // 派生 timestamp（秒）— 所有下游消费者统一使用
                frame.timestamp = static_cast<double>(frame.timestampNs) / 1e9;

                // 入队（无锁，不阻塞主线程 UI）
                m_queue.enqueue(frame);
            }
            // 清空缓冲区，防止下一轮 recv 重复入队旧帧
            recvBuf.clear();
        }
        // 超时无数据时短暂休眠，避免 CPU 空转
        if (got == 0)
            QThread::msleep(1);
    }
}
