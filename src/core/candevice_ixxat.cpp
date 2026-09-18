#include "candevice_ixxat.h"
#include "logging.h"

#include <QCoreApplication>
#include <QLibrary>

#include <cstring>

namespace {

constexpr unsigned long kOk = 0;
constexpr unsigned long kAlreadyInit = 0xE0010011u;
constexpr unsigned long kTimeout = 0xE001000Bu;
constexpr unsigned long kNoData = 0xE001000Eu;
constexpr unsigned long kNoMore = 0xE001000Fu;
constexpr unsigned long kRxEmpty = 0xE0010012u;
constexpr unsigned kInfinite = 0xFFFFFFFFu;

constexpr unsigned char kMsgData = 0;
constexpr unsigned char kMsgError = 2;
constexpr unsigned char kFlagRtr = 0x40;
constexpr unsigned char kFlagExt = 0x80;
constexpr unsigned char kAddEdl = 0x04;
constexpr unsigned char kAddFdr = 0x08;
constexpr unsigned char kAddEsi = 0x10;

constexpr unsigned char kOpStandard = 0x01;
constexpr unsigned char kOpExtended = 0x02;
constexpr unsigned char kExExtData = 0x01;
constexpr unsigned char kExFastData = 0x02;
constexpr unsigned char kFilterPass = 0x02;

constexpr unsigned kFeatStdAndExt = 0x00000002u;
constexpr unsigned kFeatExtData = 0x00002000u;
constexpr unsigned kFeatFastData = 0x00004000u;

struct Guid {
    unsigned data1;
    unsigned short data2;
    unsigned short data3;
    char data4[8];
};

union VciId {
    unsigned char raw[8];
    long long asInt64;
};

struct VciDeviceInfo {
    VciId objectId;
    Guid deviceClass;
    unsigned char driverMajor;
    unsigned char driverMinor;
    unsigned short driverBuild;
    unsigned char hwBranch;
    unsigned char hwMajor;
    unsigned char hwMinor;
    unsigned char hwBuild;
    union {
        char asChar[16];
        Guid asGuid;
    } uniqueHardwareId;
    char description[128];
    char manufacturer[126];
    unsigned short driverRelease;
};

struct CanBtp {
    unsigned mode;
    unsigned bps;
    unsigned short ts1;
    unsigned short ts2;
    unsigned short sjw;
    unsigned short tdo;
};

struct CanMsgInfo {
    unsigned char type;
    unsigned char addFlags;
    unsigned char flags;
    unsigned char accept;
};

struct CanMsg2 {
    unsigned time;
    unsigned reserved;
    unsigned msgId;
    CanMsgInfo info;
    unsigned char data[64];
};

struct CanCaps2 {
    unsigned short ctrlType;
    unsigned short busCoupling;
    unsigned features;
    unsigned canClkFreq;
    CanBtp sdrMin;
    CanBtp sdrMax;
    CanBtp fdrMin;
    CanBtp fdrMax;
    unsigned tscClkFreq;
    unsigned tscDivisor;
    unsigned cmsClkFreq;
    unsigned cmsDivisor;
    unsigned cmsMaxTicks;
    unsigned dtxClkFreq;
    unsigned dtxDivisor;
    unsigned dtxMaxTicks;
};

CanBtp preset(int baud, bool dataPhase)
{
    CanBtp b;
    std::memset(&b, 0, sizeof(b));
    const int rate = baud > 0 ? baud : (dataPhase ? 2000000 : 500000);
    b.bps = static_cast<unsigned>(rate);
    if (!dataPhase) {
        b.ts1 = 6400;
        b.ts2 = 1600;
        b.sjw = 1600;
    } else if (rate >= 8000000) {
        b.ts1 = 400;
        b.ts2 = 100;
        b.sjw = 100;
        b.tdo = 250;
    } else if (rate >= 4000000) {
        b.ts1 = 800;
        b.ts2 = 200;
        b.sjw = 200;
        b.tdo = 800;
    } else if (rate == 500000) {
        b.ts1 = 6400;
        b.ts2 = 1600;
        b.sjw = 1600;
        b.tdo = 6400;
    } else {
        b.ts1 = 1600;
        b.ts2 = 400;
        b.sjw = 400;
        b.tdo = 1600;
    }
    return b;
}

QString trimFixed(const char *s, int n)
{
    int len = 0;
    while (len < n && s[len])
        ++len;
    return QString::fromLocal8Bit(s, len).trimmed();
}

} // namespace

CanDeviceIxxat::Api &CanDeviceIxxat::api()
{
    static Api a;
    static bool tried = false;
    if (tried)
        return a;
    tried = true;

#ifndef _WIN32
    OPENBUS_LOG_WARN("Ixxat", "VCI4 is Windows-only in this build");
    return a;
#else
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/drivers/ixxat/vendor/vcinpl2.dll"),
        exeDir + QStringLiteral("/vcinpl2.dll"),
        QStringLiteral("vcinpl2"),
        QStringLiteral("vcinpl2.dll"),
    };

    QLibrary *lib = nullptr;
    for (const QString &c : candidates) {
        auto *tryLib = new QLibrary(c);
        if (tryLib->load()) {
            lib = tryLib;
            OPENBUS_LOG_INFO("Ixxat", "loaded {}", c.toStdString());
            break;
        }
        delete tryLib;
    }
    if (!lib) {
        OPENBUS_LOG_WARN("Ixxat",
            "vcinpl2.dll not found (install IXXAT VCI4 or place under "
            "drivers/ixxat/vendor)");
        return a;
    }

    a.Initialize = reinterpret_cast<decltype(a.Initialize)>(lib->resolve("vciInitialize"));
    a.FormatError = reinterpret_cast<decltype(a.FormatError)>(lib->resolve("vciFormatErrorA"));
    if (!a.FormatError)
        a.FormatError = reinterpret_cast<decltype(a.FormatError)>(lib->resolve("vciFormatError"));
    a.EnumOpen = reinterpret_cast<decltype(a.EnumOpen)>(lib->resolve("vciEnumDeviceOpen"));
    a.EnumClose = reinterpret_cast<decltype(a.EnumClose)>(lib->resolve("vciEnumDeviceClose"));
    a.EnumNext = reinterpret_cast<decltype(a.EnumNext)>(lib->resolve("vciEnumDeviceNext"));
    a.DeviceOpen = reinterpret_cast<decltype(a.DeviceOpen)>(lib->resolve("vciDeviceOpen"));
    a.DeviceClose = reinterpret_cast<decltype(a.DeviceClose)>(lib->resolve("vciDeviceClose"));
    a.ChannelOpen = reinterpret_cast<decltype(a.ChannelOpen)>(lib->resolve("canChannelOpen"));
    a.ChannelInit = reinterpret_cast<decltype(a.ChannelInit)>(lib->resolve("canChannelInitialize"));
    a.ChannelActivate = reinterpret_cast<decltype(a.ChannelActivate)>(
        lib->resolve("canChannelActivate"));
    a.ChannelClose = reinterpret_cast<decltype(a.ChannelClose)>(lib->resolve("canChannelClose"));
    a.ReadMessage = reinterpret_cast<decltype(a.ReadMessage)>(lib->resolve("canChannelReadMessage"));
    a.TxPost = reinterpret_cast<decltype(a.TxPost)>(lib->resolve("canChannelPostMessage"));
    a.ControlOpen = reinterpret_cast<decltype(a.ControlOpen)>(lib->resolve("canControlOpen"));
    a.ControlInit = reinterpret_cast<decltype(a.ControlInit)>(lib->resolve("canControlInitialize"));
    a.ControlClose = reinterpret_cast<decltype(a.ControlClose)>(lib->resolve("canControlClose"));
    a.ControlStart = reinterpret_cast<decltype(a.ControlStart)>(lib->resolve("canControlStart"));
    a.ControlGetCaps = reinterpret_cast<decltype(a.ControlGetCaps)>(
        lib->resolve("canControlGetCaps"));
    a.WaitRx = reinterpret_cast<decltype(a.WaitRx)>(lib->resolve("canChannelWaitRxEvent"));

    a.ok = a.Initialize && a.EnumOpen && a.EnumNext && a.EnumClose && a.DeviceOpen
        && a.DeviceClose && a.ChannelOpen && a.ChannelInit && a.ChannelActivate
        && a.ChannelClose && a.ReadMessage && a.TxPost && a.ControlOpen
        && a.ControlInit && a.ControlClose && a.ControlStart && a.ControlGetCaps;
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Ixxat", "vcinpl2.dll missing required exports");
        lib->unload();
        delete lib;
        return a;
    }

    const Hr st = a.Initialize();
    if (st != kOk && st != kAlreadyInit) {
        OPENBUS_LOG_ERROR("Ixxat", "vciInitialize failed: {}", statusText(st).toStdString());
        a.ok = false;
        lib->unload();
        delete lib;
        return a;
    }
    return a;
#endif
}

QString CanDeviceIxxat::statusText(Hr hr)
{
    const Api &a = api();
    if (a.FormatError) {
        char buf[256] = {};
        a.FormatError(hr, buf, sizeof(buf));
        const QString s = QString::fromLocal8Bit(buf).trimmed();
        if (!s.isEmpty())
            return s;
    }
    return QStringLiteral("hr=0x%1").arg(hr, 8, 16, QLatin1Char('0'));
}

bool CanDeviceIxxat::quietRead(Hr hr)
{
    return hr == kTimeout || hr == kNoData || hr == kRxEmpty || hr == kNoMore;
}

CanDeviceIxxat::Found CanDeviceIxxat::findDevice(int index)
{
    Found found;
    const Api &a = api();
    if (!a.ok || index < 0)
        return found;

    Handle en = nullptr;
    if (a.EnumOpen(&en) != kOk || !en)
        return found;

    for (int i = 0; i <= index; ++i) {
        VciDeviceInfo info;
        std::memset(&info, 0, sizeof(info));
        const Hr st = a.EnumNext(en, &info);
        if (st != kOk) {
            a.EnumClose(en);
            return found;
        }
        if (i != index)
            continue;

        std::memcpy(found.objectId, info.objectId.raw, sizeof(found.objectId));
        QString name = trimFixed(info.description, int(sizeof(info.description)));
        if (name.isEmpty())
            name = QStringLiteral("IXXAT #%1").arg(index);
        const QString hw = trimFixed(info.uniqueHardwareId.asChar, 16);
        if (!hw.isEmpty())
            name += QStringLiteral(" %1").arg(hw);
        found.name = name;
        found.ok = true;

        Handle dev = nullptr;
        if (a.DeviceOpen(&info.objectId, &dev) == kOk && dev) {
            int channels = 0;
            bool fd = false;
            for (unsigned ch = 0; ch < 4; ++ch) {
                Handle ctl = nullptr;
                if (a.ControlOpen(dev, ch, &ctl) != kOk || !ctl)
                    break;
                CanCaps2 caps;
                std::memset(&caps, 0, sizeof(caps));
                if (a.ControlGetCaps(ctl, &caps) == kOk) {
                    if (caps.features & (kFeatExtData | kFeatFastData))
                        fd = true;
                }
                a.ControlClose(ctl);
                ++channels;
            }
            a.DeviceClose(dev);
            if (channels > 0)
                found.channels = channels;
            found.fdCapable = fd;
        }
    }
    a.EnumClose(en);
    return found;
}

CanDeviceIxxat::CanDeviceIxxat(int deviceIndex)
    : m_preferredIndex(deviceIndex)
{
}

CanDeviceIxxat::~CanDeviceIxxat()
{
    close();
}

bool CanDeviceIxxat::isAvailable()
{
    return api().ok;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceIxxat::enumerate()
{
    std::vector<DeviceInfo> out;
    const Api &a = api();
    if (!a.ok)
        return out;

    Handle en = nullptr;
    if (a.EnumOpen(&en) != kOk || !en)
        return out;

    struct Row {
        VciId id;
        QString name;
    };
    std::vector<Row> rows;
    for (int i = 0; i < 32; ++i) {
        VciDeviceInfo info;
        std::memset(&info, 0, sizeof(info));
        if (a.EnumNext(en, &info) != kOk)
            break;
        Row row;
        row.id = info.objectId;
        row.name = trimFixed(info.description, int(sizeof(info.description)));
        const QString hw = trimFixed(info.uniqueHardwareId.asChar, 16);
        if (row.name.isEmpty())
            row.name = QStringLiteral("IXXAT #%1").arg(i);
        if (!hw.isEmpty())
            row.name += QStringLiteral(" %1").arg(hw);
        rows.push_back(row);
    }
    a.EnumClose(en);

    for (int i = 0; i < int(rows.size()); ++i) {
        DeviceInfo di;
        di.brand = Brand::Ixxat;
        di.deviceType = i;
        di.deviceIndex = i;
        di.channels = 1;
        di.driverId = QStringLiteral("ixxat");
        di.hasHwTimestamp = true;
        di.name = rows[size_t(i)].name;

        Handle dev = nullptr;
        if (a.DeviceOpen(&rows[size_t(i)].id, &dev) == kOk && dev) {
            int channels = 0;
            bool fd = false;
            for (unsigned ch = 0; ch < 4; ++ch) {
                Handle ctl = nullptr;
                if (a.ControlOpen(dev, ch, &ctl) != kOk || !ctl)
                    break;
                CanCaps2 caps;
                std::memset(&caps, 0, sizeof(caps));
                if (a.ControlGetCaps(ctl, &caps) == kOk
                    && (caps.features & (kFeatExtData | kFeatFastData)))
                    fd = true;
                a.ControlClose(ctl);
                ++channels;
            }
            a.DeviceClose(dev);
            if (channels > 0)
                di.channels = channels;
            if (fd)
                di.name += QStringLiteral(" (FD)");
        }
        out.push_back(di);
    }
    return out;
}

bool CanDeviceIxxat::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const Api &a = api();
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Ixxat", "vcinpl2.dll not available");
        return false;
    }

    m_channel = channel < 0 ? 0 : channel;

    int index = m_preferredIndex;
    if (index < 0) {
        const auto attached = enumerate();
        if (devIndex >= 0 && devIndex < int(attached.size()))
            index = attached[size_t(devIndex)].deviceType;
        else if (!attached.empty())
            index = attached.front().deviceType;
        else
            index = 0;
    }

    const Found found = findDevice(index);
    if (!found.ok) {
        OPENBUS_LOG_ERROR("Ixxat", "device index {} not found", index);
        return false;
    }
    m_deviceName = found.name;
    if (m_channel >= found.channels) {
        OPENBUS_LOG_ERROR("Ixxat", "channel {} out of range (count {})",
                          m_channel, found.channels);
        return false;
    }

    Handle dev = nullptr;
    Hr st = a.DeviceOpen(const_cast<unsigned char *>(found.objectId), &dev);
    if (st != kOk || !dev) {
        OPENBUS_LOG_ERROR("Ixxat", "vciDeviceOpen failed: {}", statusText(st).toStdString());
        return false;
    }
    m_device = dev;

    Handle ch = nullptr;
    st = a.ChannelOpen(m_device, unsigned(m_channel), 0, &ch);
    if (st != kOk || !ch) {
        OPENBUS_LOG_ERROR("Ixxat", "canChannelOpen failed: {}", statusText(st).toStdString());
        close();
        return false;
    }
    m_channelH = ch;

    st = a.ChannelInit(m_channelH, 1024, 1, 128, 1, 0, kFilterPass);
    if (st != kOk) {
        OPENBUS_LOG_ERROR("Ixxat", "canChannelInitialize failed: {}",
                          statusText(st).toStdString());
        close();
        return false;
    }
    a.ChannelActivate(m_channelH, 1);

    Handle ctl = nullptr;
    st = a.ControlOpen(m_device, unsigned(m_channel), &ctl);
    if (st != kOk || !ctl) {
        OPENBUS_LOG_ERROR("Ixxat", "canControlOpen failed: {}", statusText(st).toStdString());
        close();
        return false;
    }
    m_control = ctl;

    CanCaps2 caps;
    std::memset(&caps, 0, sizeof(caps));
    a.ControlGetCaps(m_control, &caps);
    const bool fdHw = (caps.features & (kFeatExtData | kFeatFastData)) != 0;
    m_canFd = canFd && fdHw;
    if (canFd && !m_canFd) {
        OPENBUS_LOG_WARN("Ixxat",
            "channel {} classic-only; opening Classic CAN", m_channel);
    }
    if (caps.tscDivisor != 0 && caps.tscClkFreq != 0)
        m_tickHz = double(caps.tscClkFreq) / double(caps.tscDivisor);

    unsigned char op = kOpStandard;
    if (caps.features & kFeatStdAndExt)
        op = static_cast<unsigned char>(kOpStandard | kOpExtended);
    else
        op = static_cast<unsigned char>(kOpStandard | kOpExtended);

    unsigned char ex = 0;
    if (m_canFd) {
        if (caps.features & kFeatExtData)
            ex = static_cast<unsigned char>(ex | kExExtData);
        if (caps.features & kFeatFastData)
            ex = static_cast<unsigned char>(ex | kExFastData);
    }

    CanBtp sdr = preset(arbBaud > 0 ? arbBaud : 500000, false);
    CanBtp fdr = preset(dataBaud > 0 ? dataBaud : 2000000, true);
    st = a.ControlInit(m_control, op, ex, kFilterPass, kFilterPass, 0, 0, &sdr, &fdr);
    if (st != kOk && (op & kOpExtended)) {
        op = kOpStandard;
        st = a.ControlInit(m_control, op, ex, kFilterPass, kFilterPass, 0, 0, &sdr, &fdr);
    }
    if (st != kOk) {
        OPENBUS_LOG_ERROR("Ixxat", "canControlInitialize failed: {}",
                          statusText(st).toStdString());
        close();
        return false;
    }

    st = a.ControlStart(m_control, 1);
    if (st != kOk) {
        OPENBUS_LOG_ERROR("Ixxat", "canControlStart failed: {}", statusText(st).toStdString());
        close();
        return false;
    }

    for (int i = 0; i < 8; ++i) {
        CanMsg2 junk;
        if (a.ReadMessage(m_channelH, 0, &junk) != kOk)
            break;
    }

    m_opened = true;
    OPENBUS_LOG_INFO("Ixxat", "opened {} (idx={}, ch={}, arb={}, data={}, fd={})",
                     m_deviceName.toStdString(), index, m_channel, arbBaud, dataBaud, m_canFd);
    return true;
}

void CanDeviceIxxat::close()
{
    const Api &a = api();
    if (m_control && a.ok) {
        if (a.ControlStart)
            a.ControlStart(m_control, 0);
        if (a.ControlClose)
            a.ControlClose(m_control);
    }
    m_control = nullptr;

    if (m_channelH && a.ok && a.ChannelClose)
        a.ChannelClose(m_channelH);
    m_channelH = nullptr;

    if (m_device && a.ok && a.DeviceClose)
        a.DeviceClose(m_device);
    m_device = nullptr;

    if (m_opened)
        OPENBUS_LOG_INFO("Ixxat", "closed {}", m_deviceName.toStdString());
    m_opened = false;
}

int CanDeviceIxxat::send(const CanFrame &frame)
{
    if (!m_opened || !m_channelH)
        return 0;
    const Api &a = api();
    if (!a.TxPost)
        return 0;

    CanMsg2 msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.info.type = kMsgData;
    msg.msgId = frame.id & 0x1FFFFFFFu;
    if (frame.extended)
        msg.info.flags = static_cast<unsigned char>(msg.info.flags | kFlagExt);
    const int maxLen = m_canFd ? 64 : 8;
    const int nbytes = qMin(maxLen, frame.data.size());
    msg.info.flags = static_cast<unsigned char>(
        (msg.info.flags & 0xF0) | (CanFrame::lengthToDlc(nbytes) & 0x0F));
    if (m_canFd && (frame.fd || nbytes > 8 || frame.bitrateSwitch)) {
        msg.info.addFlags = static_cast<unsigned char>(msg.info.addFlags | kAddEdl);
        if (frame.bitrateSwitch)
            msg.info.addFlags = static_cast<unsigned char>(msg.info.addFlags | kAddFdr);
        if (frame.errorState)
            msg.info.addFlags = static_cast<unsigned char>(msg.info.addFlags | kAddEsi);
    }
    if (nbytes > 0)
        std::memcpy(msg.data, frame.data.constData(), size_t(nbytes));

    return a.TxPost(m_channelH, &msg) == kOk ? 1 : 0;
}

int CanDeviceIxxat::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
    if (!m_opened || !m_channelH)
        return 0;
    const Api &a = api();
    if (!a.ReadMessage)
        return 0;

    unsigned wait = 0;
    if (timeoutMs < 0)
        wait = kInfinite;
    else if (timeoutMs > 0)
        wait = static_cast<unsigned>(timeoutMs);

    while (outFrames.size() < 256) {
        CanMsg2 msg;
        std::memset(&msg, 0, sizeof(msg));
        const Hr st = a.ReadMessage(m_channelH, wait, &msg);
        if (st != kOk) {
            if (!quietRead(st))
                OPENBUS_LOG_DEBUG("Ixxat", "read: {}", statusText(st).toStdString());
            break;
        }
        wait = 0;
        if (msg.info.type != kMsgData && msg.info.type != kMsgError)
            continue;

        CanFrame frame;
        frame.id = msg.msgId & 0x1FFFFFFFu;
        frame.extended = (msg.info.flags & kFlagExt) != 0;
        frame.fd = (msg.info.addFlags & kAddEdl) != 0;
        frame.bitrateSwitch = (msg.info.addFlags & kAddFdr) != 0;
        frame.errorState = (msg.info.addFlags & kAddEsi) != 0;
        if (msg.info.type == kMsgError)
            frame.id |= 0x20000000u;
        const int dlc = msg.info.flags & 0x0F;
        const int nbytes = frame.fd ? CanFrame::dlcToLength(static_cast<quint8>(dlc))
                                     : int(qMin(8, dlc));
        frame.dlc = static_cast<quint8>(dlc);
        frame.data = QByteArray(reinterpret_cast<const char *>(msg.data), nbytes);
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;
        if (m_tickHz > 0 && msg.time != 0)
            frame.timestampNs = static_cast<quint64>(double(msg.time) * 1e9 / m_tickHz);
        outFrames.push_back(std::move(frame));
    }
    return static_cast<int>(outFrames.size());
}

int CanDeviceIxxat::pendingCount() const
{
    if (!m_opened || !m_channelH)
        return 0;
    const Api &a = api();
    if (!a.WaitRx)
        return 1;
    return quietRead(a.WaitRx(m_channelH, 0)) ? 0 : 1;
}

QString CanDeviceIxxat::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("IXXAT") : m_deviceName;
}
