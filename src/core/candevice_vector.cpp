#include "candevice_vector.h"
#include "logging.h"

#include <QCoreApplication>
#include <QLibrary>

#include <cstring>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

// ---- XL status / constants (vxlapi.h subset) ----
constexpr short kXlSuccess = 0;
constexpr short kXlErrQueueEmpty = 10;
constexpr long kXlInvalidPort = -1;

constexpr unsigned kXlBusTypeCan = 0x00000001u;
constexpr unsigned kXlBusCompatibleCan = kXlBusTypeCan;
constexpr unsigned kXlInterfaceV3 = 3;
constexpr unsigned kXlInterfaceV4 = 4;
constexpr unsigned kXlActivateResetClock = 8;
constexpr unsigned kXlRxQueueClassic = 4096;
constexpr unsigned kXlRxQueueFd = 16384;

constexpr unsigned kXlReceiveMsg = 1;
constexpr unsigned kXlTransmitMsg = 10;
constexpr unsigned kXlCanExtMsgId = 0x80000000u;
constexpr unsigned kXlMsgFlagErrorFrame = 0x01;
constexpr unsigned kXlMsgFlagRemote = 0x10;

constexpr unsigned kXlCanEvTagRxOk = 1024;
constexpr unsigned kXlCanEvTagTxOk = 1028;
constexpr unsigned short kXlCanEvTagTxMsg = 0x0440;

constexpr unsigned kXlCanTxFlagEdl = 0x0001;
constexpr unsigned kXlCanTxFlagBrs = 0x0002;
constexpr unsigned kXlCanRxFlagEdl = 0x0001;
constexpr unsigned kXlCanRxFlagBrs = 0x0002;
constexpr unsigned kXlCanRxFlagEsi = 0x0004;
constexpr unsigned kXlCanRxFlagEf = 0x0200;

constexpr unsigned kXlChannelFlagCanFdIso = 0x80000000u;
constexpr unsigned kXlChannelFlagCanFdBosch = 0x20000000u;

constexpr unsigned kXlMaxChannels = 64;
constexpr unsigned kXlMaxName = 32;
constexpr unsigned kXlCanMaxData = 64;

using XlUint64 = unsigned long long;
using XlAccess = XlUint64;
using XlPortHandle = long;
using XlStatus = short;

#pragma pack(push, 1)

struct XlCanMsg {
    unsigned long id;
    unsigned short flags;
    unsigned short dlc;
    XlUint64 res1;
    unsigned char data[8];
    XlUint64 res2;
};

struct XlChipState {
    unsigned char busStatus;
    unsigned char txErrorCounter;
    unsigned char rxErrorCounter;
};

struct XlSyncPulse {
    unsigned char pulseCode;
    unsigned char reserved[3];
    XlUint64 time;
};

union XlTagData {
    XlCanMsg msg;
    XlChipState chipState;
    XlSyncPulse syncPulse;
    unsigned char raw[32];
};

struct XlEvent {
    unsigned char tag;
    unsigned char chanIndex;
    unsigned short transId;
    unsigned short portHandle;
    unsigned char flags;
    unsigned char reserved;
    XlUint64 timeStamp;
    XlTagData tagData;
};

struct XlBusParamsCan {
    unsigned bitRate;
    unsigned char sjw;
    unsigned char tseg1;
    unsigned char tseg2;
    unsigned char sam;
    unsigned char outputMode;
    unsigned char reserved[7];
    unsigned char canOpMode;
};

struct XlBusParamsCanFd {
    unsigned arbitrationBitRate;
    unsigned char sjwAbr;
    unsigned char tseg1Abr;
    unsigned char tseg2Abr;
    unsigned char samAbr;
    unsigned char outputMode;
    unsigned char sjwDbr;
    unsigned char tseg1Dbr;
    unsigned char tseg2Dbr;
    unsigned dataBitRate;
    unsigned char canOpMode;
};

union XlBusParamsData {
    XlBusParamsCan can;
    XlBusParamsCanFd canFD;
    unsigned char raw[28];
};

struct XlBusParams {
    unsigned busType;
    XlBusParamsData data;
};

struct XlChannelConfig {
    char name[kXlMaxName];
    unsigned char hwType;
    unsigned char hwIndex;
    unsigned char hwChannel;
    unsigned short transceiverType;
    unsigned short transceiverState;
    unsigned short configError;
    unsigned char channelIndex;
    XlUint64 channelMask;
    unsigned channelCapabilities;
    unsigned channelBusCapabilities;
    unsigned char isOnBus;
    unsigned connectedBusType;
    XlBusParams busParams;
    unsigned doNotUse;
    unsigned driverVersion;
    unsigned interfaceVersion;
    unsigned rawData[10];
    unsigned serialNumber;
    unsigned articleNumber;
    char transceiverName[kXlMaxName];
    unsigned specialCabFlags;
    unsigned dominantTimeout;
    unsigned char dominantRecessiveDelay;
    unsigned char recessiveDominantDelay;
    unsigned char connectionInfo;
    unsigned char currentlyAvailableTimestamps;
    unsigned short minimalSupplyVoltage;
    unsigned short maximalSupplyVoltage;
    unsigned maximalBaudrate;
    unsigned char fpgaCoreCapabilities;
    unsigned char specialDeviceStatus;
    unsigned short channelBusActiveCapabilities;
    unsigned short breakOffset;
    unsigned short delimiterOffset;
    unsigned reserved[3];
};

struct XlDriverConfig {
    unsigned dllVersion;
    unsigned channelCount;
    unsigned reserved[10];
    XlChannelConfig channel[kXlMaxChannels];
};

struct XlCanFdConf {
    unsigned arbitrationBitRate;
    unsigned sjwAbr;
    unsigned tseg1Abr;
    unsigned tseg2Abr;
    unsigned dataBitRate;
    unsigned sjwDbr;
    unsigned tseg1Dbr;
    unsigned tseg2Dbr;
    unsigned reserved[2];
};

#pragma pack(pop)

// FD RX/TX events use 8-byte packing (vxlapi.h pshpack8)
#pragma pack(push, 8)

struct XlCanTxMsg {
    unsigned canId;
    unsigned msgFlags;
    unsigned char dlc;
    unsigned char reserved[7];
    unsigned char data[kXlCanMaxData];
};

struct XlCanTxEvent {
    unsigned short tag;
    unsigned short transId;
    unsigned char channelIndex;
    unsigned char reserved[3];
    union {
        XlCanTxMsg canMsg;
    } tagData;
};

struct XlCanEvRxMsg {
    unsigned canId;
    unsigned msgFlags;
    unsigned crc;
    unsigned char reserved1[12];
    unsigned short totalBitCnt;
    unsigned char dlc;
    unsigned char reserved[5];
    unsigned char data[kXlCanMaxData];
};

struct XlCanEvError {
    unsigned char errorCode;
    unsigned char reserved[95];
};

struct XlCanEvChipState {
    unsigned char busStatus;
    unsigned char txErrorCounter;
    unsigned char rxErrorCounter;
    unsigned char reserved;
    unsigned reserved0;
};

struct XlCanRxEvent {
    unsigned size;
    unsigned short tag;
    unsigned short channelIndex;
    unsigned userHandle;
    unsigned short flagsChip;
    unsigned short reserved0;
    XlUint64 reserved1;
    XlUint64 timeStampSync;
    union {
        unsigned char raw[96];
        XlCanEvRxMsg canRxOkMsg;
        XlCanEvRxMsg canTxOkMsg;
        XlCanEvError canError;
        XlCanEvChipState canChipState;
    } tagData;
};

#pragma pack(pop)

int dlcCodeToLen(unsigned char dlc, bool fd)
{
    if (!fd)
        return int(qMin(unsigned(8), unsigned(dlc)));
    return CanFrame::dlcToLength(dlc);
}

} // namespace

// ============================================================
//  Shared vxlapi (never unload — same rule as PEAK / Kvaser)
// ============================================================

CanDeviceVector::Api &CanDeviceVector::api()
{
    static Api a;
    static bool tried = false;
    if (tried)
        return a;
    tried = true;

#ifndef _WIN32
    OPENBUS_LOG_WARN("Vector", "XL Driver Library is Windows-only");
    return a;
#else
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#  ifdef _WIN64
        exeDir + QStringLiteral("/drivers/vector/vendor/vxlapi64.dll"),
        exeDir + QStringLiteral("/vxlapi64.dll"),
        QStringLiteral("vxlapi64"),
        QStringLiteral("vxlapi64.dll"),
#  endif
        exeDir + QStringLiteral("/drivers/vector/vendor/vxlapi.dll"),
        exeDir + QStringLiteral("/vxlapi.dll"),
        QStringLiteral("vxlapi"),
        QStringLiteral("vxlapi.dll"),
    };

    QLibrary *lib = nullptr;
    for (const QString &c : candidates) {
        auto *tryLib = new QLibrary(c);
        if (tryLib->load()) {
            lib = tryLib;
            OPENBUS_LOG_INFO("Vector", "loaded {}", c.toStdString());
            break;
        }
        delete tryLib;
    }
    if (!lib) {
        OPENBUS_LOG_WARN("Vector",
            "vxlapi64.dll / vxlapi.dll not found (install Vector Driver Setup "
            "or place under drivers/vector/vendor)");
        return a;
    }

    a.OpenDriver = reinterpret_cast<decltype(a.OpenDriver)>(lib->resolve("xlOpenDriver"));
    a.CloseDriver = reinterpret_cast<decltype(a.CloseDriver)>(lib->resolve("xlCloseDriver"));
    a.GetDriverConfig = reinterpret_cast<decltype(a.GetDriverConfig)>(
        lib->resolve("xlGetDriverConfig"));
    a.OpenPort = reinterpret_cast<decltype(a.OpenPort)>(lib->resolve("xlOpenPort"));
    a.ClosePort = reinterpret_cast<decltype(a.ClosePort)>(lib->resolve("xlClosePort"));
    a.ActivateChannel = reinterpret_cast<decltype(a.ActivateChannel)>(
        lib->resolve("xlActivateChannel"));
    a.DeactivateChannel = reinterpret_cast<decltype(a.DeactivateChannel)>(
        lib->resolve("xlDeactivateChannel"));
    a.CanSetChannelBitrate = reinterpret_cast<decltype(a.CanSetChannelBitrate)>(
        lib->resolve("xlCanSetChannelBitrate"));
    a.CanFdSetConfiguration = reinterpret_cast<decltype(a.CanFdSetConfiguration)>(
        lib->resolve("xlCanFdSetConfiguration"));
    a.CanSetChannelMode = reinterpret_cast<decltype(a.CanSetChannelMode)>(
        lib->resolve("xlCanSetChannelMode"));
    a.CanSetChannelOutput = reinterpret_cast<decltype(a.CanSetChannelOutput)>(
        lib->resolve("xlCanSetChannelOutput"));
    a.SetNotification = reinterpret_cast<decltype(a.SetNotification)>(
        lib->resolve("xlSetNotification"));
    a.FlushReceiveQueue = reinterpret_cast<decltype(a.FlushReceiveQueue)>(
        lib->resolve("xlFlushReceiveQueue"));
    a.GetReceiveQueueLevel = reinterpret_cast<decltype(a.GetReceiveQueueLevel)>(
        lib->resolve("xlGetReceiveQueueLevel"));
    a.Receive = reinterpret_cast<decltype(a.Receive)>(lib->resolve("xlReceive"));
    a.CanTransmit = reinterpret_cast<decltype(a.CanTransmit)>(lib->resolve("xlCanTransmit"));
    a.CanReceive = reinterpret_cast<decltype(a.CanReceive)>(lib->resolve("xlCanReceive"));
    a.CanTransmitEx = reinterpret_cast<decltype(a.CanTransmitEx)>(
        lib->resolve("xlCanTransmitEx"));
    a.GetErrorString = reinterpret_cast<decltype(a.GetErrorString)>(
        lib->resolve("xlGetErrorString"));

    const bool classicOk = a.OpenDriver && a.GetDriverConfig && a.OpenPort && a.ClosePort
        && a.ActivateChannel && a.DeactivateChannel && a.CanSetChannelBitrate
        && a.Receive && a.CanTransmit;
    const bool fdOk = a.CanFdSetConfiguration && a.CanReceive && a.CanTransmitEx;
    a.ok = classicOk; // FD APIs optional; open falls back to classic
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Vector", "vxlapi missing required exports");
        lib->unload();
        delete lib;
        return a;
    }
    if (!fdOk) {
        OPENBUS_LOG_WARN("Vector",
            "FD exports missing (xlCanFdSetConfiguration/xlCanReceive/"
            "xlCanTransmitEx); classic CAN only");
    }

    const XlStatus st = a.OpenDriver();
    if (st != kXlSuccess) {
        OPENBUS_LOG_ERROR("Vector", "xlOpenDriver failed: {}",
                          statusText(st).toStdString());
        a.ok = false;
        lib->unload();
        delete lib;
        return a;
    }

    // Intentionally leak QLibrary so vxlapi stays loaded for process lifetime
    (void)fdOk;
    return a;
#endif
}

QString CanDeviceVector::statusText(XlStatus st)
{
    const Api &a = api();
    if (a.GetErrorString) {
        if (char *s = a.GetErrorString(st))
            return QString::fromLocal8Bit(s);
    }
    return QStringLiteral("status=%1").arg(st);
}

bool CanDeviceVector::lookupChannel(int channelIndex, XlAccess *maskOut, QString *nameOut,
                                    bool *fdCapableOut)
{
    const Api &a = api();
    if (!a.ok || !a.GetDriverConfig || channelIndex < 0)
        return false;

    XlDriverConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    if (a.GetDriverConfig(&cfg) != kXlSuccess)
        return false;
    if (unsigned(channelIndex) >= cfg.channelCount || unsigned(channelIndex) >= kXlMaxChannels)
        return false;

    const XlChannelConfig &ch = cfg.channel[unsigned(channelIndex)];
    if (!(ch.channelBusCapabilities & kXlBusCompatibleCan))
        return false;

    if (maskOut)
        *maskOut = ch.channelMask ? ch.channelMask
                                  : (XlAccess(1) << unsigned(ch.channelIndex));
    if (nameOut) {
        *nameOut = QString::fromLocal8Bit(ch.name).trimmed();
        if (nameOut->isEmpty())
            *nameOut = QStringLiteral("Vector ch%1").arg(channelIndex);
    }
    if (fdCapableOut) {
        *fdCapableOut = (ch.channelCapabilities
                         & (kXlChannelFlagCanFdIso | kXlChannelFlagCanFdBosch))
            != 0;
    }
    return true;
}

// ============================================================

CanDeviceVector::CanDeviceVector(int channelIndex)
    : m_preferredChannel(channelIndex)
{
}

CanDeviceVector::~CanDeviceVector()
{
    close();
}

bool CanDeviceVector::isAvailable()
{
    return api().ok;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceVector::enumerate()
{
    std::vector<DeviceInfo> out;
    const Api &a = api();
    if (!a.ok || !a.GetDriverConfig)
        return out;

    XlDriverConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    if (a.GetDriverConfig(&cfg) != kXlSuccess || cfg.channelCount == 0)
        return out;

    const unsigned n = qMin(cfg.channelCount, kXlMaxChannels);
    for (unsigned i = 0; i < n; ++i) {
        const XlChannelConfig &ch = cfg.channel[i];
        if (!(ch.channelBusCapabilities & kXlBusCompatibleCan))
            continue;

        DeviceInfo di;
        di.brand = Brand::Vector;
        di.deviceType = int(ch.channelIndex);
        di.deviceIndex = int(ch.channelIndex);
        di.channels = 1;
        di.driverId = QStringLiteral("vector");
        di.hasHwTimestamp = true;

        QString name = QString::fromLocal8Bit(ch.name).trimmed();
        if (name.isEmpty())
            name = QStringLiteral("Vector ch%1").arg(ch.channelIndex);
        const bool fd = (ch.channelCapabilities
                         & (kXlChannelFlagCanFdIso | kXlChannelFlagCanFdBosch))
            != 0;
        if (fd)
            name += QStringLiteral(" (FD)");
        if (ch.hwType == 1) // XL_HWTYPE_VIRTUAL
            name += QStringLiteral(" [virtual]");
        if (ch.serialNumber != 0)
            name += QStringLiteral(" SN%1").arg(ch.serialNumber);
        di.name = name;
        out.push_back(di);
    }
    return out;
}

bool CanDeviceVector::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const Api &a = api();
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Vector", "vxlapi not available");
        return false;
    }

    m_channel = channel < 0 ? 0 : channel;

    int xlCh = -1;
    if (m_preferredChannel >= 0) {
        xlCh = m_preferredChannel;
    } else {
        const auto attached = enumerate();
        if (devIndex >= 0 && devIndex < int(attached.size()))
            xlCh = attached[size_t(devIndex)].deviceType;
        else if (!attached.empty())
            xlCh = attached.front().deviceType;
        else
            xlCh = 0;
    }

    XlAccess mask = 0;
    QString name;
    if (!lookupChannel(xlCh, &mask, &name, &m_fdCapable)) {
        OPENBUS_LOG_ERROR("Vector", "channel {} not found or not CAN-capable", xlCh);
        return false;
    }
    m_accessMask = mask;
    m_deviceName = name;

    m_canFd = canFd && m_fdCapable && a.CanFdSetConfiguration && a.CanReceive
        && a.CanTransmitEx;
    if (canFd && !m_canFd) {
        OPENBUS_LOG_WARN("Vector",
            "channel {} classic-only or FD API missing; opening Classic CAN", xlCh);
    }

    const unsigned iface = m_canFd ? kXlInterfaceV4 : kXlInterfaceV3;
    const unsigned queue = m_canFd ? kXlRxQueueFd : kXlRxQueueClassic;

    XlPortHandle port = kXlInvalidPort;
    XlAccess permission = m_accessMask;
    char appName[32] = "openbus";
    XlStatus st = a.OpenPort(&port, appName, m_accessMask, &permission, queue, iface,
                             kXlBusTypeCan);
    if (st != kXlSuccess || port == kXlInvalidPort) {
        OPENBUS_LOG_ERROR("Vector", "xlOpenPort failed: {}", statusText(st).toStdString());
        return false;
    }
    m_port = port;

    // Init access required to set bitrate
    if ((permission & m_accessMask) == 0) {
        OPENBUS_LOG_WARN("Vector",
            "no init permission on channel {} (another app may hold it)", xlCh);
    }

    if (m_canFd) {
        XlCanFdConf conf;
        std::memset(&conf, 0, sizeof(conf));
        conf.arbitrationBitRate = unsigned(arbBaud > 0 ? arbBaud : 500000);
        conf.dataBitRate = unsigned(dataBaud > 0 ? dataBaud : 2000000);
        // Defaults match python-can VectorBus (80 MHz segment set)
        conf.sjwAbr = 2;
        conf.tseg1Abr = 6;
        conf.tseg2Abr = 3;
        conf.sjwDbr = 2;
        conf.tseg1Dbr = 6;
        conf.tseg2Dbr = 3;
        st = a.CanFdSetConfiguration(m_port, m_accessMask, &conf);
        if (st != kXlSuccess) {
            OPENBUS_LOG_ERROR("Vector", "xlCanFdSetConfiguration failed: {}",
                              statusText(st).toStdString());
            a.ClosePort(m_port);
            m_port = kXlInvalidPort;
            return false;
        }
    } else {
        const unsigned long bitrate = static_cast<unsigned long>(
            arbBaud > 0 ? arbBaud : 500000);
        st = a.CanSetChannelBitrate(m_port, m_accessMask, bitrate);
        if (st != kXlSuccess) {
            OPENBUS_LOG_ERROR("Vector", "xlCanSetChannelBitrate failed: {}",
                              statusText(st).toStdString());
            a.ClosePort(m_port);
            m_port = kXlInvalidPort;
            return false;
        }
    }

    if (a.CanSetChannelOutput)
        a.CanSetChannelOutput(m_port, m_accessMask, 1); // XL_OUTPUT_MODE_NORMAL
    if (a.CanSetChannelMode)
        a.CanSetChannelMode(m_port, m_accessMask, 1, 0); // tx receipts on

#ifdef _WIN32
    m_notifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_notifyEvent && a.SetNotification) {
        st = a.SetNotification(m_port, &m_notifyEvent, 1);
        if (st != kXlSuccess) {
            OPENBUS_LOG_WARN("Vector", "xlSetNotification failed: {}",
                             statusText(st).toStdString());
            CloseHandle(static_cast<HANDLE>(m_notifyEvent));
            m_notifyEvent = nullptr;
        }
    }
#endif

    st = a.ActivateChannel(m_port, m_accessMask, kXlBusTypeCan, kXlActivateResetClock);
    if (st != kXlSuccess) {
        OPENBUS_LOG_ERROR("Vector", "xlActivateChannel failed: {}",
                          statusText(st).toStdString());
#ifdef _WIN32
        if (m_notifyEvent) {
            CloseHandle(static_cast<HANDLE>(m_notifyEvent));
            m_notifyEvent = nullptr;
        }
#endif
        a.ClosePort(m_port);
        m_port = kXlInvalidPort;
        return false;
    }

    if (a.FlushReceiveQueue)
        a.FlushReceiveQueue(m_port);

    m_opened = true;
    OPENBUS_LOG_INFO("Vector", "opened {} (ch={}, arb={}, data={}, fd={})",
                     m_deviceName.toStdString(), xlCh, arbBaud, dataBaud, m_canFd);
    return true;
}

void CanDeviceVector::close()
{
    if (!m_opened && m_port == kXlInvalidPort)
        return;

    const Api &a = api();
    if (m_port != kXlInvalidPort && a.ok) {
        if (a.DeactivateChannel)
            a.DeactivateChannel(m_port, m_accessMask);
        if (a.ClosePort)
            a.ClosePort(m_port);
    }
    m_port = kXlInvalidPort;
    m_accessMask = 0;
    m_opened = false;

#ifdef _WIN32
    if (m_notifyEvent) {
        CloseHandle(static_cast<HANDLE>(m_notifyEvent));
        m_notifyEvent = nullptr;
    }
#endif

    OPENBUS_LOG_INFO("Vector", "closed {}", m_deviceName.toStdString());
}

int CanDeviceVector::send(const CanFrame &frame)
{
    if (!m_opened || m_port == kXlInvalidPort)
        return 0;

    const Api &a = api();
    if (m_canFd) {
        if (!a.CanTransmitEx)
            return 0;
        XlCanTxEvent ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.tag = kXlCanEvTagTxMsg;
        ev.transId = 0xFFFF;
        ev.channelIndex = 0;
        unsigned id = frame.id & 0x1FFFFFFFu;
        if (frame.extended)
            id |= kXlCanExtMsgId;
        ev.tagData.canMsg.canId = id;
        ev.tagData.canMsg.msgFlags = 0;
        if (frame.fd || m_canFd) {
            ev.tagData.canMsg.msgFlags |= kXlCanTxFlagEdl;
            if (frame.bitrateSwitch)
                ev.tagData.canMsg.msgFlags |= kXlCanTxFlagBrs;
        }
        const int nbytes = qMin(64, frame.data.size());
        ev.tagData.canMsg.dlc = CanFrame::lengthToDlc(nbytes);
        if (nbytes > 0)
            std::memcpy(ev.tagData.canMsg.data, frame.data.constData(), size_t(nbytes));

        unsigned sent = 0;
        const XlStatus st = a.CanTransmitEx(m_port, m_accessMask, 1, &sent, &ev);
        return (st == kXlSuccess && sent > 0) ? 1 : 0;
    }

    if (!a.CanTransmit)
        return 0;
    XlEvent ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.tag = static_cast<unsigned char>(kXlTransmitMsg);
    unsigned long id = frame.id & 0x1FFFFFFFu;
    if (frame.extended)
        id |= kXlCanExtMsgId;
    ev.tagData.msg.id = id;
    ev.tagData.msg.flags = 0;
    const int nbytes = qMin(8, frame.data.size());
    ev.tagData.msg.dlc = static_cast<unsigned short>(nbytes);
    if (nbytes > 0)
        std::memcpy(ev.tagData.msg.data, frame.data.constData(), size_t(nbytes));

    unsigned count = 1;
    const XlStatus st = a.CanTransmit(m_port, m_accessMask, &count, &ev);
    return (st == kXlSuccess && count > 0) ? 1 : 0;
}

int CanDeviceVector::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
    if (!m_opened || m_port == kXlInvalidPort)
        return 0;

    const Api &a = api();

#ifdef _WIN32
    if (outFrames.empty() && timeoutMs != 0 && m_notifyEvent) {
        const DWORD to = timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);
        WaitForSingleObject(static_cast<HANDLE>(m_notifyEvent), to);
    } else if (outFrames.empty() && timeoutMs > 0 && !m_notifyEvent) {
        Sleep(static_cast<DWORD>(qMin(timeoutMs, 5)));
    }
#else
    Q_UNUSED(timeoutMs);
#endif

    if (m_canFd) {
        if (!a.CanReceive)
            return 0;
        while (outFrames.size() < 256) {
            XlCanRxEvent ev;
            std::memset(&ev, 0, sizeof(ev));
            const XlStatus st = a.CanReceive(m_port, &ev);
            if (st == kXlErrQueueEmpty)
                break;
            if (st != kXlSuccess) {
                OPENBUS_LOG_DEBUG("Vector", "xlCanReceive: {}",
                                  statusText(st).toStdString());
                break;
            }
            if (ev.tag != kXlCanEvTagRxOk && ev.tag != kXlCanEvTagTxOk)
                continue;

            const XlCanEvRxMsg &msg = (ev.tag == kXlCanEvTagTxOk)
                ? ev.tagData.canTxOkMsg
                : ev.tagData.canRxOkMsg;

            CanFrame frame;
            frame.id = msg.canId & 0x1FFFFFFFu;
            frame.extended = (msg.canId & kXlCanExtMsgId) != 0;
            frame.fd = (msg.msgFlags & kXlCanRxFlagEdl) != 0;
            frame.bitrateSwitch = (msg.msgFlags & kXlCanRxFlagBrs) != 0;
            frame.errorState = (msg.msgFlags & kXlCanRxFlagEsi) != 0;
            if (msg.msgFlags & kXlCanRxFlagEf)
                frame.id |= 0x20000000u;

            const int nbytes = dlcCodeToLen(msg.dlc, frame.fd);
            frame.dlc = msg.dlc;
            frame.data = QByteArray(reinterpret_cast<const char *>(msg.data), nbytes);
            frame.channel = static_cast<quint8>(m_channel + 1);
            frame.direction = (ev.tag == kXlCanEvTagTxOk) ? CanFrame::Tx : CanFrame::Rx;
            if (ev.timeStampSync != 0)
                frame.timestampNs = ev.timeStampSync; // Vector timebase; manager may rebase

            outFrames.push_back(std::move(frame));
        }
        return static_cast<int>(outFrames.size());
    }

    if (!a.Receive)
        return 0;
    while (outFrames.size() < 256) {
        XlEvent ev;
        std::memset(&ev, 0, sizeof(ev));
        unsigned count = 1;
        const XlStatus st = a.Receive(m_port, &count, &ev);
        if (st == kXlErrQueueEmpty || count == 0)
            break;
        if (st != kXlSuccess) {
            OPENBUS_LOG_DEBUG("Vector", "xlReceive: {}", statusText(st).toStdString());
            break;
        }
        if (ev.tag != kXlReceiveMsg)
            continue;

        CanFrame frame;
        frame.id = quint32(ev.tagData.msg.id) & 0x1FFFFFFFu;
        frame.extended = (ev.tagData.msg.id & kXlCanExtMsgId) != 0;
        frame.fd = false;
        if (ev.tagData.msg.flags & kXlMsgFlagErrorFrame)
            frame.id |= 0x20000000u;
        const int nbytes = int(qMin(unsigned(8), unsigned(ev.tagData.msg.dlc)));
        frame.dlc = CanFrame::lengthToDlc(nbytes);
        frame.data = QByteArray(reinterpret_cast<const char *>(ev.tagData.msg.data), nbytes);
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;
        if (ev.timeStamp != 0)
            frame.timestampNs = ev.timeStamp;

        outFrames.push_back(std::move(frame));
    }
    return static_cast<int>(outFrames.size());
}

int CanDeviceVector::pendingCount() const
{
    if (!m_opened || m_port == kXlInvalidPort)
        return 0;
    const Api &a = api();
    if (!a.GetReceiveQueueLevel)
        return 1;
    int level = 0;
    if (a.GetReceiveQueueLevel(m_port, &level) != kXlSuccess)
        return 0;
    return level;
}

QString CanDeviceVector::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("Vector XL") : m_deviceName;
}
