#include "candevice_zlg.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QThread>
#include <algorithm>
#include <cstring>

// ============================================================
//  ZLG SDK 结构体定义（与 zlgcan.h 内存布局一致）
//  直接定义，避免依赖厂商头文件
// ============================================================

namespace {

// ZLG SDK 常量
#define TYPE_CAN    0
#define TYPE_CANFD  4
#define STATUS_OK   1

/// ZLG 通道初始化配置（与 zlgcan.h ZCAN_CHANNEL_INIT_CONFIG 一致）
/// 波特率通过 ZCAN_SetValue 设置，此结构仅配置滤波和模式
struct ZCAN_ChannelInitConfig {
    unsigned int can_type;       // TYPE_CAN=0, TYPE_CANFD=4
    unsigned int filter;         // 0=全部接收, 1=自定义滤波
    unsigned int mode;           // 0=Normal, 1=ListenOnly
    unsigned int acc_code;       // 验收码
    unsigned int acc_mask;       // 屏蔽码
    // CAN FD 额外字段
    unsigned char padEnable;
    unsigned char padByte;
    unsigned char rmtEnable;
    unsigned char txoutCtrl;
    unsigned char isSTB;
    unsigned char listRTRMode;
    unsigned char reserved[10];  // 填充以保证结构体大小兼容
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

    // 解析函数符号 (函数名大小写必须与 DLL 导出一致)
    m_fn_open     = (fn_OpenDevice)  m_dll.resolve("ZCAN_OpenDevice");
    m_fn_close    = (fn_CloseDevice) m_dll.resolve("ZCAN_CloseDevice");
    m_fn_init     = (fn_InitCan)     m_dll.resolve("ZCAN_InitCAN");
    m_fn_start    = (fn_StartCan)    m_dll.resolve("ZCAN_StartCAN");
    m_fn_send     = (fn_Transmit)    m_dll.resolve("ZCAN_Transmit");
    m_fn_recvNum  = (fn_GetRecvNum)  m_dll.resolve("ZCAN_GetReceiveNum");
    m_fn_recv     = (fn_Receive)     m_dll.resolve("ZCAN_Receive");
    m_fn_reset    = (fn_ResetCan)    m_dll.resolve("ZCAN_ResetCAN");
    m_fn_devInfo  = (fn_GetDevInfo)  m_dll.resolve("ZCAN_GetDeviceInf");
    m_fn_setVal   = (fn_SetValue)    m_dll.resolve("ZCAN_SetValue");

    if (!m_fn_open || !m_fn_close || !m_fn_init || !m_fn_start ||
        !m_fn_send || !m_fn_recvNum || !m_fn_recv || !m_fn_setVal) {
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
    m_fn_setVal  = nullptr;
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

    // 设置波特率 — 通过 ZCAN_SetValue (key 为 "ch/baud_rate" 或 "ch/canfd_abit_baud_rate")
    QString chPrefix = QStringLiteral("%1/").arg(channel);
    if (canFd) {
        // CAN FD: 仲裁段 + 数据段波特率
        QString arbKey = chPrefix + QStringLiteral("canfd_abit_baud_rate");
        QString dataKey = chPrefix + QStringLiteral("canfd_dbit_baud_rate");
        m_fn_setVal(m_devHandle, arbKey.toUtf8().constData(),
                    QString::number(arbBaud).toUtf8().constData());
        m_fn_setVal(m_devHandle, dataKey.toUtf8().constData(),
                    QString::number(dataBaud).toUtf8().constData());
    } else {
        // Classic CAN: 单一波特率
        QString baudKey = chPrefix + QStringLiteral("baud_rate");
        m_fn_setVal(m_devHandle, baudKey.toUtf8().constData(),
                    QString::number(arbBaud).toUtf8().constData());
    }

    // 初始化 CAN 通道 — ZCAN_InitCAN 返回 CHANNEL_HANDLE
    ZCAN_ChannelInitConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.can_type = canFd ? TYPE_CANFD : TYPE_CAN;
    cfg.filter = 0;       // 全部接收
    cfg.mode = 0;          // Normal 模式
    cfg.acc_code = 0;
    cfg.acc_mask = 0xffffffff;

    m_channelHandle = m_fn_init(m_devHandle, static_cast<unsigned int>(channel), &cfg);
    if (!m_channelHandle) {
        SIN_LOG_ERROR("CanDeviceZLG", "ZCAN_InitCAN failed: ch={}", channel);
        m_fn_close(m_devHandle);
        m_devHandle = nullptr;
        return false;
    }

    // 启动 CAN — ZCAN_StartCAN 接收 CHANNEL_HANDLE
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

    int sent = m_fn_send(m_channelHandle, &tx, 1);
    return sent;
}

int CanDeviceZLG::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened || !m_fn_recv)
        return 0;

    // 先查队列中帧数
    unsigned int pending = m_fn_recvNum(m_channelHandle);
    if (pending == 0) {
        if (timeoutMs <= 0)
            return 0;
        // 短暂等待后重试
        QThread::msleep(1);
        pending = m_fn_recvNum(m_channelHandle);
        if (pending == 0)
            return 0;
    }

    // 批量接收（最多 256 帧/次）
    int toRead = static_cast<int>(std::min(pending, 256u));
    std::vector<ZCAN_TransmitData> recvBuf(toRead);

    int got = m_fn_recv(m_channelHandle,
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
    if (!m_opened || !m_fn_recvNum || !m_channelHandle)
        return 0;
    return static_cast<int>(m_fn_recvNum(m_channelHandle));
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
        if (m_fn_reset && m_channelHandle)
            return m_fn_reset(m_channelHandle) == STATUS_OK;
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
    QLibrary dll(findZlgDllPath());
    return dll.load() && dll.resolve("ZCAN_OpenDevice") != nullptr;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceZLG::enumerate()
{
    std::vector<DeviceInfo> list;
    if (!isAvailable())
        return list;

    // ZLG 设备枚举：尝试打开各类型 0-15 号设备
    QLibrary dll(findZlgDllPath());
    if (!dll.load())
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
