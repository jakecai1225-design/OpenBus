#include "candevice_peak.h"
#include "logging.h"

#include <chrono>
#include <cstring>

// PCAN-Basic 常量
static constexpr unsigned short PCAN_BAUD_500K  = 0x001C;
static constexpr unsigned short PCAN_BAUD_1M    = 0x0014;
static constexpr unsigned short PCAN_BAUD_250K  = 0x011C;
static constexpr unsigned short PCAN_BAUD_125K  = 0x031C;
static constexpr unsigned short PCAN_BAUD_100K  = 0x0431;
static constexpr unsigned short PCAN_BAUD_50K   = 0x4B1C;  // approx

static constexpr unsigned int  PCAN_OK              = 0x00;
static constexpr unsigned int  PCAN_ERROR_QRCVEMPTY  = 0x20;
static constexpr unsigned int  PCAN_ERROR_ILLOPER    = 0x01;

// MSGTYPE 标志
static constexpr unsigned char MSGTYPE_STANDARD = 0x00;
static constexpr unsigned char MSGTYPE_EXTENDED = 0x02;
static constexpr unsigned char MSGTYPE_RTR       = 0x01;
static constexpr unsigned char MSGTYPE_FD        = 0x04;
static constexpr unsigned char MSGTYPE_BRS       = 0x08;
static constexpr unsigned char MSGTYPE_ESI       = 0x10;

// PCAN_GetValue 参数
static constexpr unsigned char PCAN_DEVICE_NAME = 0x03;

// ============================================================
//  CanDevicePEAK 实现
// ============================================================

CanDevicePEAK::CanDevicePEAK(DeviceType devType)
    : m_devType(devType)
{
}

CanDevicePEAK::~CanDevicePEAK()
{
    close();
    unloadDll();
}

// ---- DLL 加载 ----

bool CanDevicePEAK::loadDll()
{
    if (m_dll.isLoaded())
        return true;

    m_dll.setFileName(QStringLiteral("PCANUSB"));
    if (!m_dll.load()) {
        // 尝试常见路径
        m_dll.setFileName(QStringLiteral("PCANUSB.dll"));
        if (!m_dll.load()) {
            OPENBUS_LOG_DEBUG("CanDevicePEAK", "PCANUSB.dll not found");
            return false;
        }
    }

    m_fn_init    = (fn_Initialize)   m_dll.resolve("CAN_Initialize");
    m_fn_initFD  = (fn_InitializeFD) m_dll.resolve("CAN_InitializeFD");
    m_fn_read    = (fn_Read)         m_dll.resolve("CAN_Read");
    m_fn_readFD  = (fn_ReadFD)       m_dll.resolve("CAN_ReadFD");
    m_fn_write   = (fn_Write)        m_dll.resolve("CAN_Write");
    m_fn_writeFD = (fn_WriteFD)      m_dll.resolve("CAN_WriteFD");
    m_fn_uninit  = (fn_Uninitialize) m_dll.resolve("CAN_Uninitialize");
    m_fn_getVal  = (fn_GetValue)     m_dll.resolve("CAN_GetValue");
    m_fn_status  = (fn_GetStatus)    m_dll.resolve("CAN_GetStatus");

    if (!m_fn_init || !m_fn_read || !m_fn_write || !m_fn_uninit) {
        OPENBUS_LOG_ERROR("CanDevicePEAK", "Failed to resolve core functions");
        unloadDll();
        return false;
    }

    OPENBUS_LOG_INFO("CanDevicePEAK", "PCANUSB.dll loaded successfully");
    return true;
}

void CanDevicePEAK::unloadDll()
{
    if (m_dll.isLoaded())
        m_dll.unload();

    m_fn_init = nullptr;
    m_fn_initFD = nullptr;
    m_fn_read = nullptr;
    m_fn_readFD = nullptr;
    m_fn_write = nullptr;
    m_fn_writeFD = nullptr;
    m_fn_uninit = nullptr;
    m_fn_getVal = nullptr;
    m_fn_status = nullptr;
}

// ---- 设备操作 ----

bool CanDevicePEAK::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        return true;

    if (!loadDll()) {
        OPENBUS_LOG_ERROR("CanDevicePEAK", "Cannot open: PCANUSB.dll not available");
        return false;
    }

    m_devIndex = devIndex;
    m_channel = channel;
    m_canFd = canFd;
    m_startClock = std::chrono::steady_clock::now();

    // PCAN 通道号 = DeviceType + channel
    m_pcanHandle = static_cast<TPCANHandle>(m_devType + channel);

    TPCANStatus status;

    if (canFd && m_fn_initFD) {
        // CAN FD: 使用字符串波特率配置
        // 格式: "fclk_m=N,nominal_brp=B,nominal_tseg1=T1,nominal_tseg2=T2,nominal_sjw=S,data_brp=.."
        // 简化: 直接用波特率数值字符串，让 PCAN 驱动自动计算
        QString fdStr = QStringLiteral("%1").arg(arbBaud);
        status = m_fn_initFD(m_pcanHandle, fdStr.toUtf8().constData());
    } else {
        // Classic CAN: BTR0BTR1
        unsigned short btr;
        switch (arbBaud) {
        case 1000000: btr = PCAN_BAUD_1M;   break;
        case 500000:  btr = PCAN_BAUD_500K; break;
        case 250000:  btr = PCAN_BAUD_250K; break;
        case 125000:  btr = PCAN_BAUD_125K; break;
        case 100000:  btr = PCAN_BAUD_100K; break;
        case 50000:   btr = PCAN_BAUD_50K;  break;
        default:      btr = PCAN_BAUD_500K; break;
        }
        status = m_fn_init(m_pcanHandle, btr, 0, 0, 0);
    }

    if (status != PCAN_OK) {
        OPENBUS_LOG_ERROR("CanDevicePEAK", "CAN_Initialize failed: status=0x{:08X}",
                      status);
        return false;
    }

    // 获取设备名称
    if (m_fn_getVal) {
        char nameBuf[256] = {0};
        status = m_fn_getVal(m_pcanHandle, PCAN_DEVICE_NAME, nameBuf, sizeof(nameBuf));
        if (status == PCAN_OK)
            m_deviceName = QString::fromUtf8(nameBuf);
    }
    if (m_deviceName.isEmpty())
        m_deviceName = QStringLiteral("PCAN-USB FD Ch%1").arg(channel + 1);

    m_opened = true;
    OPENBUS_LOG_INFO("CanDevicePEAK", "Device opened: {}, baud={}, fd={}",
                 m_deviceName.toStdString(), arbBaud, canFd);
    return true;
}

void CanDevicePEAK::close()
{
    if (!m_opened)
        return;

    if (m_fn_uninit)
        m_fn_uninit(m_pcanHandle);

    m_opened = false;
    OPENBUS_LOG_INFO("CanDevicePEAK", "Device closed");
}

int CanDevicePEAK::send(const CanFrame &frame)
{
    if (!m_opened || !m_fn_write)
        return 0;

    if (m_canFd && m_fn_writeFD) {
        TPCANMsgFD msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.ID = frame.id;
        msg.MSGTYPE = (frame.extended ? MSGTYPE_EXTENDED : MSGTYPE_STANDARD)
                     | MSGTYPE_FD
                     | (frame.bitrateSwitch ? MSGTYPE_BRS : 0)
                     | (frame.errorState ? MSGTYPE_ESI : 0);
        msg.DLC = frame.dlc;
        int len = CanFrame::dlcToLength(frame.dlc);
        std::memcpy(msg.DATA, frame.data.constData(), std::min(len, 64));

        return (m_fn_writeFD(m_pcanHandle, &msg) == PCAN_OK) ? 1 : 0;
    } else {
        TPCANMsg msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.ID = frame.id;
        msg.MSGTYPE = (frame.extended ? MSGTYPE_EXTENDED : MSGTYPE_STANDARD)
                     | (frame.id & 0x40000000 ? MSGTYPE_RTR : 0);  // CAN_RTR_FLAG
        int len = static_cast<int>(std::min<qsizetype>(frame.data.size(), 8));
        msg.LEN = static_cast<unsigned char>(len);
        std::memcpy(msg.DATA, frame.data.constData(), len);

        return (m_fn_write(m_pcanHandle, &msg) == PCAN_OK) ? 1 : 0;
    }
}

int CanDevicePEAK::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened)
        return 0;

    outFrames.clear();
    int totalRead = 0;

    // 循环读取直到队列空或达到上限
    while (totalRead < 256) {
        CanFrame frame;
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;

        if (m_canFd && m_fn_readFD) {
            TPCANMsgFD msg;
            TPCANTimestamp ts;
            std::memset(&msg, 0, sizeof(msg));
            std::memset(&ts, 0, sizeof(ts));

            TPCANStatus status = m_fn_readFD(m_pcanHandle, &msg, &ts);
            if (status != PCAN_OK) {
                if (status != PCAN_ERROR_QRCVEMPTY)
                    OPENBUS_LOG_DEBUG("CanDevicePEAK", "CAN_ReadFD status=0x{:08X}", status);
                break;
            }

            frame.id = msg.ID & 0x1FFFFFFF;
            frame.extended = (msg.MSGTYPE & MSGTYPE_EXTENDED) != 0;
            frame.fd = true;
            frame.bitrateSwitch = (msg.MSGTYPE & MSGTYPE_BRS) != 0;
            frame.errorState = (msg.MSGTYPE & MSGTYPE_ESI) != 0;
            frame.dlc = msg.DLC;
            int len = CanFrame::dlcToLength(msg.DLC);
            frame.data = QByteArray(reinterpret_cast<const char*>(msg.DATA), std::min(len, 64));
            // timestampNs 由 CanDeviceManager 用 steady_clock 统一填充
        } else if (m_fn_read) {
            TPCANMsg msg;
            TPCANTimestamp ts;
            std::memset(&msg, 0, sizeof(msg));
            std::memset(&ts, 0, sizeof(ts));

            TPCANStatus status = m_fn_read(m_pcanHandle, &msg, &ts);
            if (status != PCAN_OK) {
                if (status != PCAN_ERROR_QRCVEMPTY)
                    OPENBUS_LOG_DEBUG("CanDevicePEAK", "CAN_Read status=0x{:08X}", status);
                break;
            }

            frame.id = msg.ID & 0x1FFFFFFF;
            frame.extended = (msg.MSGTYPE & MSGTYPE_EXTENDED) != 0;
            frame.fd = false;
            frame.dlc = msg.LEN;
            frame.data = QByteArray(reinterpret_cast<const char*>(msg.DATA), msg.LEN);
            // timestampNs 由 CanDeviceManager 用 steady_clock 统一填充
        } else {
            break;
        }

        outFrames.push_back(std::move(frame));
        ++totalRead;
    }

    // 非阻塞模式：如果没读到数据且需要等待
    if (totalRead == 0 && timeoutMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return totalRead;
}

int CanDevicePEAK::pendingCount() const
{
    // PCAN 没有 GetReceiveNum 等价函数，使用 Read 试探
    // 返回 1 让 recv 尝试读取
    return m_opened ? 1 : 0;
}

bool CanDevicePEAK::isOpen() const
{
    return m_opened;
}

QString CanDevicePEAK::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("PEAK PCAN") : m_deviceName;
}

// ---- 静态方法 ----

bool CanDevicePEAK::isAvailable()
{
    QLibrary dll(QStringLiteral("PCANUSB"));
    if (dll.load()) {
        dll.unload();
        return true;
    }
    return false;
}

std::vector<ICanDevice::DeviceInfo> CanDevicePEAK::enumerate()
{
    std::vector<DeviceInfo> list;
    if (!isAvailable())
        return list;

    // PCAN 设备固定通道号范围
    // 尝试枚举常见的 USB 通道
    DeviceType types[] = {PCAN_USBFD, PCAN_USB, PCAN_USBPROFD};
    for (auto t : types) {
        DeviceInfo info;
        info.brand = Brand::PEAK;
        info.deviceType = t;
        info.deviceIndex = 0;
        info.channels = 1;

        // 尝试获取设备名称
        QLibrary dll(QStringLiteral("PCANUSB"));
        if (dll.load()) {
            using fn_GetValue = unsigned int (__stdcall *)(unsigned short, unsigned char, void*, int);
            auto getValue = (fn_GetValue)dll.resolve("CAN_GetValue");
            using fn_Initialize = unsigned int (__stdcall *)(unsigned short, unsigned short, int, int, int);
            auto init = (fn_Initialize)dll.resolve("CAN_Initialize");
            using fn_Uninitialize = unsigned int (__stdcall *)(unsigned short);
            auto uninit = (fn_Uninitialize)dll.resolve("CAN_Uninitialize");

            if (init && getValue && uninit) {
                // 尝试初始化以检测设备存在
                unsigned short baud500k = 0x001C;
                if (init(t, baud500k, 0, 0, 0) == 0) {  // PCAN_OK
                    char nameBuf[256] = {0};
                    if (getValue(t, PCAN_DEVICE_NAME, nameBuf, sizeof(nameBuf)) == 0)
                        info.name = QString::fromUtf8(nameBuf);
                    if (info.name.isEmpty())
                        info.name = QStringLiteral("PCAN Ch%1").arg(t - 0x50);
                    uninit(t);
                    list.push_back(info);
                }
            }
            dll.unload();
        }
    }
    return list;
}

// ---- 时间戳转换 ----

quint64 CanDevicePEAK::pcanTsToNs(const TPCANTimestamp &ts) const
{
    // PCAN 时间戳：millis (ms since device boot) + micros (us fraction)
    // 转为纳秒
    quint64 totalUs = static_cast<quint64>(ts.millis) * 1000 + ts.micros;
    // 加上溢出部分
    totalUs += static_cast<quint64>(ts.millis_overflow) * 0xFFFFFFFFULL * 1000;
    return totalUs * 1000;  // us → ns
}
