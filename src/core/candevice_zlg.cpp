#include "candevice_zlg.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QThread>
#include <algorithm>
#include <cstring>

// ============================================================
//  ZLG SDK 结构体定义（与官方 zlgcan.h / canframe.h 内存布局一致）
//  直接定义，避免依赖厂商头文件
// ============================================================

namespace {

// ZLG SDK 常量（与官方 zlgcan.h 一致）
#define TYPE_CAN      0
#define TYPE_CANFD    1
#define TYPE_ALL_DATA 2
#define STATUS_OK     1

// CAN ID 标志位（与官方 canframe.h 一致）
#define CAN_EFF_FLAG  0x80000000U  // 扩展帧标志
#define CAN_RTR_FLAG  0x40000000U  // 远程帧标志
#define CAN_ERR_FLAG  0x20000000U  // 错误帧标志
#define CAN_ID_FLAG   0x1FFFFFFFU  // ID 掩码

// CAN FD 标志位（与官方 canframe.h 一致）
#define CANFD_BRS     0x01  // Bit Rate Switch
#define CANFD_ESI     0x02  // Error State Indicator

// 数据长度
#define CAN_MAX_DLEN   8
#define CANFD_MAX_DLEN 64

/// Classic CAN 帧（与官方 canframe.h can_frame 一致）
struct can_frame {
    unsigned int  can_id;    // 32 bit: ID + EFF/RTR/ERR flags
    unsigned char can_dlc;   // 数据长度码 (0..8)
    unsigned char __pad;
    unsigned char __res0;
    unsigned char __res1;
    unsigned char data[CAN_MAX_DLEN];
};

/// CAN FD 帧（与官方 canframe.h canfd_frame 一致）
struct canfd_frame {
    unsigned int  can_id;    // 32 bit: ID + EFF/RTR/ERR flags
    unsigned char len;       // 数据长度 (0..64)
    unsigned char flags;     // CAN FD 标志 (BRS/ESI)
    unsigned char __res0;
    unsigned char __res1;
    unsigned char data[CANFD_MAX_DLEN];
};

/// ZLG 发送数据结构 — Classic CAN（与官方 zlgcan.h ZCAN_Transmit_Data 一致）
struct ZCAN_Transmit_Data {
    can_frame   frame;
    unsigned int transmit_type;
};

/// ZLG 接收数据结构 — Classic CAN（与官方 zlgcan.h ZCAN_Receive_Data 一致）
struct ZCAN_Receive_Data {
    can_frame  frame;
    unsigned long long timestamp;  // 微秒
};

/// ZLG 发送数据结构 — CAN FD（与官方 zlgcan.h ZCAN_TransmitFD_Data 一致）
struct ZCAN_TransmitFD_Data {
    canfd_frame  frame;
    unsigned int transmit_type;
};

/// ZLG 接收数据结构 — CAN FD（与官方 zlgcan.h ZCAN_ReceiveFD_Data 一致）
struct ZCAN_ReceiveFD_Data {
    canfd_frame frame;
    unsigned long long timestamp;  // 微秒
};

/// ZLG 通道初始化配置（与官方 zlgcan.h ZCAN_CHANNEL_INIT_CONFIG 一致）
/// 波特率通过 ZCAN_SetValue 设置，此结构仅配置滤波和模式
struct ZCAN_CHANNEL_INIT_CONFIG {
    unsigned int can_type;  // TYPE_CAN=0, TYPE_CANFD=1
    union {
        struct {
            unsigned int  acc_code;
            unsigned int  acc_mask;
            unsigned int  reserved;
            unsigned char filter;    // 0=全部接收
            unsigned char timing0;
            unsigned char timing1;
            unsigned char mode;      // 0=Normal, 1=ListenOnly
        } can;
        struct {
            unsigned int   acc_code;
            unsigned int   acc_mask;
            unsigned int   abit_timing;
            unsigned int   dbit_timing;
            unsigned int   brp;
            unsigned char  filter;
            unsigned char  mode;
            unsigned short pad;
            unsigned int   reserved;
        } canfd;
    };
};

/// ZLG 设备信息（与官方 zlgcan.h ZCAN_DEVICE_INFO 一致）
struct ZCAN_DEVICE_INFO {
    unsigned short hw_Version;
    unsigned short fw_Version;
    unsigned short dr_Version;
    unsigned short in_Version;
    unsigned short irq_Num;
    unsigned char  can_Num;
    unsigned char  str_Serial_Num[20];
    unsigned char  str_hw_Type[40];
    unsigned short reserved[4];
};

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

// 搜索 zlgcan.dll 路径 — 应用目录 → ZCANPRO 安装目录 → 系统 PATH
static QString findZlgDllPath()
{
    const QStringList dirs = {
        QCoreApplication::applicationDirPath(),
        QStringLiteral("C:/Program Files (x86)/ZCANPRO"),
        QStringLiteral("C:/Program Files/ZCANPRO"),
    };
    for (const auto &dir : dirs) {
        QString path = dir + QStringLiteral("/zlgcan.dll");
        if (QFile::exists(path)) {
            SIN_LOG_INFO("CanDeviceZLG", "DLL found: {}", path.toStdString());
            return path;
        }
    }
    // 回退到系统 PATH
    return QStringLiteral("zlgcan.dll");
}

bool CanDeviceZLG::loadDll()
{
    if (m_dll.isLoaded())
        return true;

    m_dll.setFileName(findZlgDllPath());
    m_dll.load();

    if (!m_dll.isLoaded()) {
        SIN_LOG_ERROR("CanDeviceZLG", "Failed to load zlgcan.dll: {}",
                       m_dll.errorString().toStdString());
        return false;
    }

    // 解析函数符号（函数名大小写必须与 DLL 导出一致）
    m_fn_open     = (fn_OpenDevice)   m_dll.resolve("ZCAN_OpenDevice");
    m_fn_close    = (fn_CloseDevice)  m_dll.resolve("ZCAN_CloseDevice");
    m_fn_init     = (fn_InitCan)      m_dll.resolve("ZCAN_InitCAN");
    m_fn_start    = (fn_StartCan)     m_dll.resolve("ZCAN_StartCAN");
    m_fn_send     = (fn_Transmit)     m_dll.resolve("ZCAN_Transmit");
    m_fn_sendFD   = (fn_TransmitFD)   m_dll.resolve("ZCAN_TransmitFD");
    m_fn_recvNum  = (fn_GetRecvNum)   m_dll.resolve("ZCAN_GetReceiveNum");
    m_fn_recv     = (fn_Receive)      m_dll.resolve("ZCAN_Receive");
    m_fn_recvFD   = (fn_ReceiveFD)    m_dll.resolve("ZCAN_ReceiveFD");
    m_fn_reset    = (fn_ResetCan)     m_dll.resolve("ZCAN_ResetCAN");
    m_fn_devInfo  = (fn_GetDevInfo)   m_dll.resolve("ZCAN_GetDeviceInf");
    m_fn_setVal   = (fn_SetValue)     m_dll.resolve("ZCAN_SetValue");
    m_fn_getAvail = (fn_GetAvailDev)  m_dll.resolve("ZCAN_GetAvailableDevices");
    m_fn_isOnline = (fn_IsDevOnline)  m_dll.resolve("ZCAN_IsDeviceOnLine");

    if (!m_fn_open || !m_fn_close || !m_fn_init || !m_fn_start ||
        !m_fn_send || !m_fn_recvNum || !m_fn_recv || !m_fn_setVal) {
        SIN_LOG_ERROR("CanDeviceZLG", "Missing required ZLG SDK functions");
        unloadDll();
        return false;
    }

    // CAN FD 函数为可选（Classic CAN 设备可能不导出）
    if (!m_fn_sendFD || !m_fn_recvFD) {
        SIN_LOG_INFO("CanDeviceZLG", "CAN FD functions not available (Classic CAN only)");
    }

    return true;
}

void CanDeviceZLG::unloadDll()
{
    if (m_dll.isLoaded())
        m_dll.unload();

    m_fn_open     = nullptr;
    m_fn_close    = nullptr;
    m_fn_init     = nullptr;
    m_fn_start    = nullptr;
    m_fn_send     = nullptr;
    m_fn_sendFD   = nullptr;
    m_fn_recvNum  = nullptr;
    m_fn_recv     = nullptr;
    m_fn_recvFD   = nullptr;
    m_fn_reset    = nullptr;
    m_fn_devInfo  = nullptr;
    m_fn_setVal   = nullptr;
    m_fn_getAvail = nullptr;
    m_fn_isOnline = nullptr;
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

    m_canFd = canFd;

    // 打开设备 — ZCAN_OpenDevice(deviceType, deviceIndex, reserved)
    m_devHandle = m_fn_open(static_cast<unsigned int>(m_devType),
                             static_cast<unsigned int>(devIndex), 0);
    if (!m_devHandle) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_OpenDevice failed: type={}, index={}",
                       static_cast<int>(m_devType), devIndex);
        return false;
    }

    m_devIndex = devIndex;
    m_channel = channel;
    m_startTime = static_cast<double>(
        QDateTime::currentMSecsSinceEpoch()) / 1000.0;

    // 设置波特率 — 通过 ZCAN_SetValue
    QString chPrefix = QStringLiteral("%1/").arg(channel);
    if (canFd) {
        // CAN FD: 仲裁段 + 数据段波特率
        QString arbKey = chPrefix + QStringLiteral("canfd_abit_baud_rate");
        QString dataKey = chPrefix + QStringLiteral("canfd_dbit_baud_rate");
        QString arbVal = QString::number(arbBaud);
        QString dataVal = QString::number(dataBaud);
        m_fn_setVal(m_devHandle, arbKey.toUtf8().constData(),
                    arbVal.toUtf8().constData());
        m_fn_setVal(m_devHandle, dataKey.toUtf8().constData(),
                    dataVal.toUtf8().constData());
    } else {
        // Classic CAN: 单一波特率
        QString baudKey = chPrefix + QStringLiteral("baud_rate");
        QString baudVal = QString::number(arbBaud);
        m_fn_setVal(m_devHandle, baudKey.toUtf8().constData(),
                    baudVal.toUtf8().constData());
    }

    // 初始化 CAN 通道 — ZCAN_InitCAN(deviceHandle, canIndex, &config)
    ZCAN_CHANNEL_INIT_CONFIG cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.can_type = canFd ? TYPE_CANFD : TYPE_CAN;

    if (canFd) {
        cfg.canfd.acc_code = 0;
        cfg.canfd.acc_mask = 0xffffffff;
        cfg.canfd.filter = 0;       // 全部接收
        cfg.canfd.mode = 0;         // Normal 模式
    } else {
        cfg.can.acc_code = 0;
        cfg.can.acc_mask = 0xffffffff;
        cfg.can.filter = 0;         // 全部接收
        cfg.can.mode = 0;           // Normal 模式
    }

    m_channelHandle = m_fn_init(m_devHandle,
                                 static_cast<unsigned int>(channel), &cfg);
    if (!m_channelHandle) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_InitCAN failed: ch={}", channel);
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
        return false;
    }

    // 启动 CAN — ZCAN_StartCAN(channelHandle)
    if (m_fn_start(m_channelHandle) != STATUS_OK) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_StartCAN failed: ch={}", channel);
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
        m_channelHandle = nullptr;
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

    // 先复位通道，再关闭设备
    if (m_channelHandle && m_fn_reset)
        m_fn_reset(m_channelHandle);
    if (m_devHandle) {
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
    }
    m_channelHandle = nullptr;

    m_opened = false;
    SIN_LOG_INFO("CanDeviceZLG", "Device closed");
}

int CanDeviceZLG::send(const CanFrame &frame)
{
    if (!m_opened || !m_fn_send)
        return 0;

    if (m_canFd && m_fn_sendFD) {
        // ---- CAN FD 发送 ----
        ZCAN_TransmitFD_Data tx;
        std::memset(&tx, 0, sizeof(tx));
        tx.transmit_type = 0;  // 正常发送

        // 编码 CAN ID: id | EFF flag
        tx.frame.can_id = frame.id;
        if (frame.extended)
            tx.frame.can_id |= CAN_EFF_FLAG;

        // 数据
        int dataLen = frame.data.size();
        if (dataLen > CANFD_MAX_DLEN) dataLen = CANFD_MAX_DLEN;
        std::memcpy(tx.frame.data, frame.data.constData(), dataLen);
        tx.frame.len = static_cast<unsigned char>(dataLen);

        // CAN FD 标志
        if (frame.bitrateSwitch)
            tx.frame.flags |= CANFD_BRS;
        if (frame.errorState)
            tx.frame.flags |= CANFD_ESI;

        return m_fn_sendFD(m_channelHandle, &tx, 1);
    } else {
        // ---- Classic CAN 发送 ----
        ZCAN_Transmit_Data tx;
        std::memset(&tx, 0, sizeof(tx));
        tx.transmit_type = 0;  // 正常发送

        // 编码 CAN ID
        tx.frame.can_id = frame.id;
        if (frame.extended)
            tx.frame.can_id |= CAN_EFF_FLAG;

        // 数据
        int dataLen = frame.data.size();
        if (dataLen > CAN_MAX_DLEN) dataLen = CAN_MAX_DLEN;
        std::memcpy(tx.frame.data, frame.data.constData(), dataLen);
        tx.frame.can_dlc = static_cast<unsigned char>(frame.dlc);

        return m_fn_send(m_channelHandle, &tx, 1);
    }
}

int CanDeviceZLG::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened || !m_fn_recv)
        return 0;

    unsigned char type = m_canFd ? TYPE_CANFD : TYPE_CAN;

    // 先查队列中帧数
    unsigned int pending = m_fn_recvNum(m_channelHandle, type);
    if (pending == 0) {
        if (timeoutMs <= 0)
            return 0;
        // 短暂等待后重试
        QThread::msleep(1);
        pending = m_fn_recvNum(m_channelHandle, type);
        if (pending == 0)
            return 0;
    }

    // 批量接收（最多 256 帧/次）
    int toRead = static_cast<int>(std::min(pending, 256u));

    if (m_canFd && m_fn_recvFD) {
        // ---- CAN FD 接收 ----
        std::vector<ZCAN_ReceiveFD_Data> recvBuf(toRead);
        int got = m_fn_recvFD(m_channelHandle, recvBuf.data(), toRead, timeoutMs);
        if (got <= 0)
            return 0;

        for (int i = 0; i < got; ++i) {
            const auto &z = recvBuf[i];
            CanFrame frame;

            // 解码 CAN ID
            frame.id = z.frame.can_id & CAN_ID_FLAG;
            frame.extended = (z.frame.can_id & CAN_EFF_FLAG) != 0;
            frame.fd = true;
            frame.bitrateSwitch = (z.frame.flags & CANFD_BRS) != 0;
            frame.errorState = (z.frame.flags & CANFD_ESI) != 0;

            int dataLen = z.frame.len;
            if (dataLen > CANFD_MAX_DLEN) dataLen = CANFD_MAX_DLEN;
            frame.dlc = CanFrame::lengthToDlc(dataLen);
            frame.data = QByteArray(reinterpret_cast<const char *>(z.frame.data), dataLen);

            frame.channel = static_cast<quint8>(m_channel + 1);
            frame.direction = CanFrame::Rx;

            // 相对时间戳（秒）
            double nowT = static_cast<double>(
                QDateTime::currentMSecsSinceEpoch()) / 1000.0;
            frame.timestamp = nowT - m_startTime;

            outFrames.push_back(frame);
        }
        return got;
    } else {
        // ---- Classic CAN 接收 ----
        std::vector<ZCAN_Receive_Data> recvBuf(toRead);
        int got = m_fn_recv(m_channelHandle, recvBuf.data(), toRead, timeoutMs);
        if (got <= 0)
            return 0;

        for (int i = 0; i < got; ++i) {
            const auto &z = recvBuf[i];
            CanFrame frame;

            // 解码 CAN ID
            frame.id = z.frame.can_id & CAN_ID_FLAG;
            frame.extended = (z.frame.can_id & CAN_EFF_FLAG) != 0;
            frame.fd = false;

            int dataLen = CanFrame::dlcToLength(z.frame.can_dlc);
            frame.dlc = z.frame.can_dlc;
            frame.data = QByteArray(reinterpret_cast<const char *>(z.frame.data), dataLen);

            frame.channel = static_cast<quint8>(m_channel + 1);
            frame.direction = CanFrame::Rx;

            // 相对时间戳（秒）
            double nowT = static_cast<double>(
                QDateTime::currentMSecsSinceEpoch()) / 1000.0;
            frame.timestamp = nowT - m_startTime;

            outFrames.push_back(frame);
        }
        return got;
    }
}

int CanDeviceZLG::pendingCount() const
{
    if (!m_opened || !m_fn_recvNum || !m_channelHandle)
        return 0;
    unsigned char type = m_canFd ? TYPE_CANFD : TYPE_CAN;
    return static_cast<int>(m_fn_recvNum(m_channelHandle, type));
}

bool CanDeviceZLG::isOpen() const
{
    return m_opened;
}

QString CanDeviceZLG::deviceName() const
{
    switch (m_devType) {
    case DEV_USBCAN_1:       return QStringLiteral("USBCAN-1");
    case DEV_USBCAN_2:       return QStringLiteral("USBCAN-2");
    case DEV_USBCAN_E_U:     return QStringLiteral("USBCAN-E-U");
    case DEV_USBCAN_2E_U:    return QStringLiteral("USBCAN-2E-U");
    case DEV_USBCAN_4E_U:    return QStringLiteral("USBCAN-4E-U");
    case DEV_USBCANFD_200U:  return QStringLiteral("USBCANFD-200U");
    case DEV_USBCANFD_100U:  return QStringLiteral("USBCANFD-100U");
    case DEV_USBCANFD_MINI:  return QStringLiteral("USBCANFD-mini");
    case DEV_USBCANFD_800U:  return QStringLiteral("USBCANFD-800U");
    default:                 return QStringLiteral("ZLG CAN Device");
    }
}

bool CanDeviceZLG::vendorCtrl(int cmd, void *param)
{
    if (!m_opened || !m_devHandle)
        return false;

    switch (cmd) {
    case CMD_RESET_CAN:
        if (m_fn_reset && m_channelHandle)
            return m_fn_reset(m_channelHandle) == STATUS_OK;
        return false;

    case CMD_GET_DEV_INFO:
        if (m_fn_devInfo && param) {
            ZCAN_DEVICE_INFO info;
            std::memset(&info, 0, sizeof(info));
            if (m_fn_devInfo(m_devHandle, &info) == STATUS_OK) {
                std::memcpy(param, &info, sizeof(info));
                return true;
            }
        }
        return false;

    case CMD_GET_BUS_STATUS:
        // ZLG 总线状态读取需要厂商特定 API，后续补充
        return false;

    default:
        return false;
    }
}

// ---- 静态方法 ----

bool CanDeviceZLG::isAvailable()
{
    QLibrary dll(findZlgDllPath());
    if (!dll.load()) {
        SIN_LOG_ERROR("CanDeviceZLG", "isAvailable: zlgcan.dll not loadable: {}",
                       dll.errorString().toStdString());
        return false;
    }
    bool ok = dll.resolve("ZCAN_OpenDevice") != nullptr;
    SIN_LOG_INFO("CanDeviceZLG", "isAvailable: {}", ok ? "true" : "false");
    return ok;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceZLG::enumerate()
{
    std::vector<DeviceInfo> list;
    if (!isAvailable())
        return list;

    QLibrary dll(findZlgDllPath());
    if (!dll.load())
        return list;

    auto fn_open     = (fn_OpenDevice)  dll.resolve("ZCAN_OpenDevice");
    auto fn_close    = (fn_CloseDevice) dll.resolve("ZCAN_CloseDevice");
    auto fn_isOnline = (fn_IsDevOnline)  dll.resolve("ZCAN_IsDeviceOnLine");
    if (!fn_open || !fn_close)
        return list;

    // 已知设备类型及其通道数
    struct TypeEntry { DeviceType type; int channels; };
    const TypeEntry types[] = {
        { DEV_USBCANFD_200U, 2 },
        { DEV_USBCANFD_100U, 1 },
        { DEV_USBCANFD_MINI, 1 },
        { DEV_USBCANFD_800U, 8 },
        { DEV_USBCAN_2E_U,   2 },
        { DEV_USBCAN_4E_U,   4 },
        { DEV_USBCAN_E_U,    1 },
        { DEV_USBCAN_1,      8 },
        { DEV_USBCAN_2,      2 },
    };

    SIN_LOG_INFO("CanDeviceZLG", "enumerate: scanning {} device types",
                 (int)(sizeof(types) / sizeof(types[0])));

    for (const auto &e : types) {
        for (int idx = 0; idx < 4; ++idx) {
            // 尝试打开设备 — ZCAN_OpenDevice(type, index, reserved)
            // 设备物理连接时返回有效句柄，否则返回 INVALID_DEVICE_HANDLE(0)
            void *h = fn_open(static_cast<unsigned int>(e.type),
                              static_cast<unsigned int>(idx), 0);
            if (!h)
                continue;

            // 打开成功，进一步检查设备是否在线
            // ZCAN_IsDeviceOnLine(deviceHandle) — 返回 1=在线
            bool online = true;
            if (fn_isOnline) {
                online = (fn_isOnline(h) == 1);
            }

            fn_close(h);

            if (!online) {
                SIN_LOG_INFO("CanDeviceZLG", "  Opened but offline: type={} idx={}",
                             static_cast<int>(e.type), idx);
                continue;
            }

            SIN_LOG_INFO("CanDeviceZLG", "  Found: type={} idx={}",
                         static_cast<int>(e.type), idx);

            DeviceInfo info;
            info.deviceType = static_cast<int>(e.type);
            info.deviceIndex = idx;
            info.channels = e.channels;
            info.name = CanDeviceZLG(e.type).deviceName() +
                QStringLiteral(" #%1").arg(idx);
            list.push_back(info);
        }
    }

    SIN_LOG_INFO("CanDeviceZLG", "enumerate: found {} devices", (int)list.size());
    return list;
}
