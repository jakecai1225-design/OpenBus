#include "candevice_zlg.h"
#include "logging.h"

#include <QDateTime>
#include <QThread>
#include <algorithm>
#include <cstring>

// ============================================================
//  ZLG SDK 结构体定义（与 zlgcan.h 内存布局一致）
//  直接定义，避免依赖厂商头文件
// ============================================================

namespace {

#pragma pack(push, 1)

/// ZLG 初始化配置（新版 zlgcan SDK）
struct ZCAN_InitConfig {
    unsigned int channel;         // 通道号
    unsigned int baudrate;        // 仲裁段波特率
    unsigned int dataBaudrate;    // 数据段波特率 (CAN FD)
    unsigned char canfd;          // 0=Classic, 1=FD
    unsigned char mode;           // 0=Normal, 1=ListenOnly
    unsigned char padEnable;
    unsigned char padByte;
    unsigned char rmtEnable;
    unsigned char txoutCtrl;
    unsigned char isSTB;
    unsigned char listRTRMode;
    unsigned int timing0;
    unsigned int timing1;
    unsigned int timing2;
    unsigned int timing3;
    unsigned int timing4;
    unsigned int timing5;
    unsigned int timing6;
    unsigned int timing7;
    unsigned int filterType;       // 0=全部接收, 1=自定义滤波
    unsigned int filterCode;
    unsigned int filterMask;
    unsigned int dataTiming0;
    unsigned int dataTiming1;
    unsigned int dataTiming2;
    unsigned int dataTiming3;
};

/// ZLG 收发数据帧（64 字节数据区，兼容 CAN FD）
struct ZCAN_TransmitData {
    unsigned char data[64];   // 数据
    unsigned char len;        // DLC (0-15)
    unsigned char msgType;    // 0=标准帧, 1=扩展帧
    unsigned char canType;    // 0=Classic, 1=FD
    unsigned char brs;        // Bit Rate Switch
    unsigned char esi;        // Error State Indicator
    unsigned char padByte;
    unsigned char reserved;
    unsigned int frameID;    // CAN ID
    unsigned int timestamp;  // 硬件时间戳 (ms)
};

#pragma pack(pop)

} // namespace

// ============================================================
//  CanDeviceZLG 实现
// ============================================================

CanDeviceZLG::CanDeviceZLG(DeviceType devType)
    : m_devType(devType)
{
}

CanDeviceZLG::~CanDeviceZLG()
{
    close();
    unloadDll();
}

// ---- DLL 加载 ----

bool CanDeviceZLG::loadDll()
{
    if (m_dll.isLoaded())
        return true;

    // 尝试多个可能的 DLL 路径/名称
    const QStringList dllNames = {
        QStringLiteral("zlgcan.dll"),
        QStringLiteral("zlgcan"),
    };

    for (const auto &name : dllNames) {
        m_dll.setFileName(name);
        if (m_dll.load()) {
            SIN_LOG_INFO("CanDeviceZLG", "DLL loaded: {}", name.toStdString());
            break;
        }
    }

    if (!m_dll.isLoaded()) {
        SIN_LOG_ERROR("CanDeviceZLG", "Failed to load zlgcan.dll: {}",
                       m_dll.errorString().toStdString());
        return false;
    }

    // 解析函数符号
    m_fn_open     = (fn_OpenDevice)  m_dll.resolve("ZCAN_OpenDevice");
    m_fn_close    = (fn_CloseDevice) m_dll.resolve("ZCAN_CloseDevice");
    m_fn_init     = (fn_InitCan)     m_dll.resolve("ZCAN_InitCan");
    m_fn_start    = (fn_StartCan)    m_dll.resolve("ZCAN_StartCan");
    m_fn_send     = (fn_Transmit)    m_dll.resolve("ZCAN_Transmit");
    m_fn_recvNum  = (fn_GetRecvNum)  m_dll.resolve("ZCAN_GetReceiveNum");
    m_fn_recv     = (fn_Receive)     m_dll.resolve("ZCAN_Receive");
    m_fn_reset    = (fn_ResetCan)    m_dll.resolve("ZCAN_ResetCan");
    m_fn_devInfo  = (fn_GetDevInfo)  m_dll.resolve("ZCAN_GetDevInfo");

    if (!m_fn_open || !m_fn_close || !m_fn_init || !m_fn_start ||
        !m_fn_send || !m_fn_recvNum || !m_fn_recv) {
        SIN_LOG_ERROR("CanDeviceZLG", "Missing required ZLG SDK functions");
        unloadDll();
        return false;
    }

    return true;
}

void CanDeviceZLG::unloadDll()
{
    if (m_dll.isLoaded())
        m_dll.unload();

    m_fn_open    = nullptr;
    m_fn_close   = nullptr;
    m_fn_init    = nullptr;
    m_fn_start   = nullptr;
    m_fn_send    = nullptr;
    m_fn_recvNum = nullptr;
    m_fn_recv    = nullptr;
    m_fn_reset   = nullptr;
    m_fn_devInfo = nullptr;
}

// ---- ICanDevice 实现 ----

bool CanDeviceZLG::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        return true;

    if (!loadDll()) {
        SIN_LOG_ERROR("CanDeviceZLG", "Cannot open: zlgcan.dll not available");
        return false;
    }

    // 打开设备
    m_devHandle = m_fn_open(static_cast<int>(m_devType), devIndex, 0);
    if (!m_devHandle) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_OpenDevice failed: type={}, index={}",
                       static_cast<int>(m_devType), devIndex);
        return false;
    }

    m_devIndex = devIndex;
    m_channel = channel;
    m_startTime = static_cast<double>(
        QDateTime::currentMSecsSinceEpoch()) / 1000.0;

    // 初始化 CAN 通道
    ZCAN_InitConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.channel = static_cast<unsigned int>(channel);
    cfg.baudrate = static_cast<unsigned int>(arbBaud);
    cfg.dataBaudrate = static_cast<unsigned int>(dataBaud);
    cfg.canfd = canFd ? 1 : 0;
    cfg.mode = 0;  // Normal 模式
    cfg.filterType = 0; // 全部接收

    if (m_fn_init(m_devHandle, cfg.channel, &cfg) != 1) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_InitCan failed: ch={}", channel);
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
        return false;
    }

    // 启动 CAN
    if (m_fn_start(m_devHandle, cfg.channel) != 1) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_StartCan failed: ch={}", channel);
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
        return false;
    }

    m_opened = true;
    SIN_LOG_INFO("CanDeviceZLG", "Device opened: type={}, index={}, ch={}, baud={}, fd={}",
                 static_cast<int>(m_devType), devIndex, channel, arbBaud, canFd);
    return true;
}

void CanDeviceZLG::close()
{
    if (!m_opened)
        return;

    if (m_devHandle) {
        if (m_fn_reset)
            m_fn_reset(m_devHandle, static_cast<unsigned int>(m_channel));
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
    }

    m_opened = false;
    SIN_LOG_INFO("CanDeviceZLG", "Device closed");
}

int CanDeviceZLG::send(const CanFrame &frame)
{
    if (!m_opened || !m_fn_send)
        return 0;

    ZCAN_TransmitData tx;
    std::memset(&tx, 0, sizeof(tx));

    // 填充数据
    int dataLen = frame.data.size();
    if (dataLen > 64) dataLen = 64;
    std::memcpy(tx.data, frame.data.constData(), dataLen);

    tx.len = frame.dlc;
    tx.msgType = frame.extended ? 1 : 0;
    tx.canType = frame.fd ? 1 : 0;
    tx.brs = frame.bitrateSwitch ? 1 : 0;
    tx.esi = frame.errorState ? 1 : 0;
    tx.frameID = frame.id;

    int sent = m_fn_send(m_devHandle, static_cast<unsigned int>(m_channel),
                         &tx, 1);
    return sent;
}

int CanDeviceZLG::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened || !m_fn_recv)
        return 0;

    // 先查队列中帧数
    unsigned int pending = m_fn_recvNum(m_devHandle,
                                          static_cast<unsigned int>(m_channel));
    if (pending == 0) {
        if (timeoutMs <= 0)
            return 0;
        // 短暂等待后重试
        QThread::msleep(1);
        pending = m_fn_recvNum(m_devHandle,
                                static_cast<unsigned int>(m_channel));
        if (pending == 0)
            return 0;
    }

    // 批量接收（最多 256 帧/次）
    int toRead = static_cast<int>(std::min(pending, 256u));
    std::vector<ZCAN_TransmitData> recvBuf(toRead);

    int got = m_fn_recv(m_devHandle, static_cast<unsigned int>(m_channel),
                        recvBuf.data(), toRead, timeoutMs);
    if (got <= 0)
        return 0;

    for (int i = 0; i < got; ++i) {
        const auto &z = recvBuf[i];

        CanFrame frame;
        frame.id = z.frameID;
        frame.extended = (z.msgType == 1);
        frame.fd = (z.canType == 1);
        frame.bitrateSwitch = (z.brs == 1);
        frame.errorState = (z.esi == 1);
        frame.dlc = z.len;
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;

        int dataLen = CanFrame::dlcToLength(z.len);
        frame.data = QByteArray(reinterpret_cast<const char *>(z.data), dataLen);

        // 相对时间戳（秒）
        double nowT = static_cast<double>(
            QDateTime::currentMSecsSinceEpoch()) / 1000.0;
        frame.timestamp = nowT - m_startTime;

        outFrames.push_back(frame);
    }

    return got;
}

int CanDeviceZLG::pendingCount() const
{
    if (!m_opened || !m_fn_recvNum || !m_devHandle)
        return 0;
    return static_cast<int>(m_fn_recvNum(m_devHandle,
        static_cast<unsigned int>(m_channel)));
}

bool CanDeviceZLG::isOpen() const
{
    return m_opened;
}

QString CanDeviceZLG::deviceName() const
{
    switch (m_devType) {
    case DEV_USBCAN_1:      return QStringLiteral("USBCAN-1");
    case DEV_USBCAN_2:      return QStringLiteral("USBCAN-2");
    case DEV_USBCANFD_200U: return QStringLiteral("USBCANFD-200U");
    case DEV_USBCANFD_100U: return QStringLiteral("USBCANFD-100U");
    case DEV_USBCAN_4E:     return QStringLiteral("USBCAN-4E");
    case DEV_PCI_CANAL:     return QStringLiteral("PCI-CANal");
    default:                return QStringLiteral("ZLG CAN Device");
    }
}

bool CanDeviceZLG::vendorCtrl(int cmd, void *param)
{
    if (!m_opened || !m_devHandle)
        return false;

    switch (cmd) {
    case CMD_RESET_CAN:
        if (m_fn_reset)
            return m_fn_reset(m_devHandle,
                static_cast<unsigned int>(m_channel)) == 1;
        return false;

    case CMD_GET_DEV_INFO:
        if (m_fn_devInfo && param) {
            return m_fn_devInfo(m_devHandle,
                static_cast<unsigned char *>(param), 512) == 1;
        }
        return false;

    case CMD_GET_BUS_STATUS:
        // ZLG 总线状态读取需要厂商特定 API
        // 后续根据实际 SDK 补充
        return false;

    default:
        return false;
    }
}

// ---- 静态方法 ----

bool CanDeviceZLG::isAvailable()
{
    QLibrary dll;
    const QStringList names = { QStringLiteral("zlgcan.dll"), QStringLiteral("zlgcan") };
    for (const auto &name : names) {
        dll.setFileName(name);
        if (dll.load()) {
            return dll.resolve("ZCAN_OpenDevice") != nullptr;
        }
    }
    return false;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceZLG::enumerate()
{
    std::vector<DeviceInfo> list;
    if (!isAvailable())
        return list;

    // ZLG 设备枚举：尝试打开各类型 0-15 号设备
    // 实际 SDK 提供 ZCAN_FindDevice，此处用 try-open 方式
    QLibrary dll;
    const QStringList names = { QStringLiteral("zlgcan.dll"), QStringLiteral("zlgcan") };
    for (const auto &name : names) {
        dll.setFileName(name);
        if (dll.load()) break;
    }
    if (!dll.isLoaded())
        return list;

    auto fn_open  = (fn_OpenDevice)  dll.resolve("ZCAN_OpenDevice");
    auto fn_close = (fn_CloseDevice) dll.resolve("ZCAN_CloseDevice");
    if (!fn_open || !fn_close)
        return list;

    // 尝试已知设备类型
    const DeviceType types[] = { DEV_USBCANFD_200U, DEV_USBCANFD_100U,
                                   DEV_USBCAN_1, DEV_USBCAN_2, DEV_USBCAN_4E };

    for (auto t : types) {
        for (int idx = 0; idx < 4; ++idx) {
            void *h = fn_open(static_cast<int>(t), idx, 0);
            if (h) {
                fn_close(h);
                DeviceInfo info;
                info.deviceType = static_cast<int>(t);
                info.deviceIndex = idx;
                info.channels = (t == DEV_USBCAN_1) ? 8 : 2;
                info.name = CanDeviceZLG(t).deviceName() +
                    QStringLiteral(" #%1").arg(idx);
                list.push_back(info);
            }
        }
    }

    return list;
}
