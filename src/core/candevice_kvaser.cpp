#include "candevice_kvaser.h"
#include "logging.h"

#include <chrono>
#include <cstring>

// canlib32 常量
static constexpr int CANLIB_CAN_STD_MESSAGE  = 0;
static constexpr int CANLIB_CAN_EXT_MESSAGE  = 1;
static constexpr int CANLIB_CANFD_MESSAGE     = 0x2000;
static constexpr int CANLIB_CANFD_BRS         = 0x4000;
static constexpr int CANLIB_CANFD_ESI         = 0x8000;
static constexpr int CANLIB_CAN_RTR           = 0x100;
static constexpr int CANLIB_CANMSG_STD        = 0;
static constexpr int CANLIB_CANMSG_EXT        = 1;

static constexpr int CANLIB_OK                = 0;
static constexpr int CANERR_NOMSG             = -1;

// canOpenFlags
static constexpr int CANLIB_FLAG_SLOW = 0x10000000;  // Acceptance filter mode
static constexpr int CANLIB_CAN_INIT_FLAG   = 0x00000001;
static constexpr int CANLIB_CAN_EXCLUSIVE   = 0x00000008;

// ============================================================
//  CanDeviceKvaser 实现 (P1 — 基础框架，待完善)
// ============================================================

CanDeviceKvaser::CanDeviceKvaser(int subType)
    : m_subType(subType)
{
}

CanDeviceKvaser::~CanDeviceKvaser()
{
    close();
    unloadDll();
}

// ---- DLL 加载 ----

bool CanDeviceKvaser::loadDll()
{
    if (m_dll.isLoaded())
        return true;

    m_dll.setFileName(QStringLiteral("canlib32"));
    if (!m_dll.load()) {
        OPENBUS_LOG_DEBUG("CanDeviceKvaser", "canlib32.dll not found");
        return false;
    }

    OPENBUS_LOG_INFO("CanDeviceKvaser", "canlib32.dll loaded successfully");
    return true;
}

void CanDeviceKvaser::unloadDll()
{
    if (m_dll.isLoaded())
        m_dll.unload();
}

// ---- 设备操作 ----

bool CanDeviceKvaser::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        return true;

    if (!loadDll()) {
        OPENBUS_LOG_ERROR("CanDeviceKvaser", "Cannot open: canlib32.dll not available");
        return false;
    }

    m_devIndex = devIndex;
    m_channel = channel;
    m_canFd = canFd;
    m_startClock = std::chrono::steady_clock::now();

    // canlib32 API 函数指针
    using fn_canOpen = int (__stdcall *)(int channel, int flags);
    auto canOpen = (fn_canOpen)m_dll.resolve("canOpenChannel");

    // 设备序号映射: devIndex * 2 + channel
    int canlibChannel = devIndex * 2 + channel;

    if (canOpen) {
        // canOpenChannel 返回 handle (>=0) 或负值 (错误)
        int h = canOpen(canlibChannel, CANLIB_CAN_INIT_FLAG | CANLIB_CAN_EXCLUSIVE);
        if (h < 0) {
            OPENBUS_LOG_ERROR("CanDeviceKvaser", "canOpenChannel failed: {}", h);
            return false;
        }
        m_canlibHandle = h;

        // 设置波特率 (canSetBusParams)
        using fn_canSetBusParams = int (__stdcall *)(int handle, int freq, int tseg1, int tseg2, int sjw, int samp, int tseg1_fd, int tseg2_fd, int sjw_fd);
        // 简化: 用 canSetBusParamsTiming
        // 实际实现需要根据 arbBaud 计算 BTR 参数
        // 暂时只记录打开成功
        OPENBUS_LOG_INFO("CanDeviceKvaser", "canlib handle={} opened, arbBaud={}", h, arbBaud);

        // canBusOn
        using fn_canBusOn = int (__stdcall *)(int handle);
        auto busOn = (fn_canBusOn)m_dll.resolve("canBusOn");
        if (busOn)
            busOn(m_canlibHandle);
    } else {
        OPENBUS_LOG_ERROR("CanDeviceKvaser", "canOpenChannel not resolved");
        return false;
    }

    m_opened = true;
    m_deviceName = QStringLiteral("Kvaser Ch%1").arg(channel + 1);
    OPENBUS_LOG_INFO("CanDeviceKvaser", "Device opened: {}", m_deviceName.toStdString());
    return true;
}

void CanDeviceKvaser::close()
{
    if (!m_opened)
        return;

    if (m_canlibHandle >= 0) {
        using fn_canBusOff = int (__stdcall *)(int handle);
        auto busOff = (fn_canBusOff)m_dll.resolve("canBusOff");
        if (busOff) busOff(m_canlibHandle);

        using fn_canClose = int (__stdcall *)(int handle);
        auto canClose = (fn_canClose)m_dll.resolve("canClose");
        if (canClose) canClose(m_canlibHandle);

        m_canlibHandle = -1;
    }

    m_opened = false;
    OPENBUS_LOG_INFO("CanDeviceKvaser", "Device closed");
}

int CanDeviceKvaser::send(const CanFrame &frame)
{
    if (!m_opened || m_canlibHandle < 0)
        return 0;

    // canWrite
    using fn_canWrite = int (__stdcall *)(int handle, int id, const void* msg, int dlc, int flag);
    auto canWrite = (fn_canWrite)m_dll.resolve("canWrite");
    if (!canWrite)
        return 0;

    int flag = frame.extended ? CANLIB_CAN_EXT_MESSAGE : CANLIB_CAN_STD_MESSAGE;
    if (m_canFd) {
        flag |= CANLIB_CANFD_MESSAGE;
        if (frame.bitrateSwitch) flag |= CANLIB_CANFD_BRS;
        if (frame.errorState) flag |= CANLIB_CANFD_ESI;
    }

    int result = canWrite(m_canlibHandle, frame.id,
                          frame.data.constData(), frame.data.size(), flag);
    return (result == CANLIB_OK) ? 1 : 0;
}

int CanDeviceKvaser::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened || m_canlibHandle < 0)
        return 0;

    outFrames.clear();

    // canReadWait
    using fn_canReadWait = int (__stdcall *)(int handle, int* id, void* msg, int* dlc, int* flag, unsigned long* time, unsigned long timeout);
    auto canReadWait = (fn_canReadWait)m_dll.resolve("canReadWait");
    if (!canReadWait)
        return 0;

    while (outFrames.size() < 256) {
        int id = 0, dlc = 0, flag = 0;
        unsigned long time = 0;
        char data[64] = {0};

        int result = canReadWait(m_canlibHandle, &id, data, &dlc, &flag, &time, timeoutMs);
        if (result != CANLIB_OK) {
            if (result != CANERR_NOMSG)
                OPENBUS_LOG_DEBUG("CanDeviceKvaser", "canReadWait: {}", result);
            break;
        }

        CanFrame frame;
        frame.id = id & 0x1FFFFFFF;
        frame.extended = (flag & CANLIB_CAN_EXT_MESSAGE) != 0;
        frame.fd = (flag & CANLIB_CANFD_MESSAGE) != 0;
        frame.bitrateSwitch = (flag & CANLIB_CANFD_BRS) != 0;
        frame.errorState = (flag & CANLIB_CANFD_ESI) != 0;
        frame.dlc = CanFrame::lengthToDlc(dlc);
        frame.data = QByteArray(data, dlc);
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;

        // timestampNs 由 CanDeviceManager 用 steady_clock 统一填充

        outFrames.push_back(std::move(frame));
        timeoutMs = 0;  // 后续非阻塞读取
    }

    return static_cast<int>(outFrames.size());
}

int CanDeviceKvaser::pendingCount() const
{
    // canReadStatus 可以检查是否有消息
    return m_opened ? 1 : 0;
}

bool CanDeviceKvaser::isOpen() const
{
    return m_opened;
}

QString CanDeviceKvaser::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("Kvaser CAN") : m_deviceName;
}

// ---- 静态方法 ----

bool CanDeviceKvaser::isAvailable()
{
    QLibrary dll(QStringLiteral("canlib32"));
    if (dll.load()) {
        dll.unload();
        return true;
    }
    return false;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceKvaser::enumerate()
{
    std::vector<DeviceInfo> list;
    if (!isAvailable())
        return list;

    // canGetNumberOfChannels
    QLibrary dll(QStringLiteral("canlib32"));
    if (dll.load()) {
        using fn_getNum = int (__stdcall *)(int* count);
        auto getNum = (fn_getNum)dll.resolve("canGetNumberOfChannels");
        if (getNum) {
            int count = 0;
            if (getNum(&count) == CANLIB_OK) {
                for (int i = 0; i < count; ++i) {
                    DeviceInfo info;
                    info.brand = Brand::Kvaser;
                    info.deviceType = 0;
                    info.deviceIndex = i;
                    info.channels = 1;
                    info.name = QStringLiteral("Kvaser #%1").arg(i);
                    list.push_back(info);
                }
            }
        }
        dll.unload();
    }
    return list;
}
