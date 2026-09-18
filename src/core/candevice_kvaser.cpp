#include "candevice_kvaser.h"
#include "logging.h"

#include <QCoreApplication>
#include <QLibrary>

#include <cstring>

namespace {

// Status (canstat.h)
constexpr int kOk = 0;
constexpr int kErrNomsg = -2;

// canOpenChannel flags
constexpr int kOpenExclusive = 0x0008;
constexpr int kOpenAcceptVirtual = 0x0020;
constexpr int kOpenCanFd = 0x0400;

// canBITRATE_xxx (classic)
constexpr long kBitrate1M = -1;
constexpr long kBitrate500K = -2;
constexpr long kBitrate250K = -3;
constexpr long kBitrate125K = -4;
constexpr long kBitrate100K = -5;
constexpr long kBitrate50K = -7;

// canFD_BITRATE_xxx
constexpr long kFdBitrate500K80 = -1000;
constexpr long kFdBitrate1M80 = -1001;
constexpr long kFdBitrate2M80 = -1002;
constexpr long kFdBitrate4M80 = -1003;
constexpr long kFdBitrate8M60 = -1004;

// canMSG_xxx / canFDMSG_xxx
constexpr unsigned kMsgRtr = 0x0001;
constexpr unsigned kMsgStd = 0x0002;
constexpr unsigned kMsgExt = 0x0004;
constexpr unsigned kMsgErrorFrame = 0x0020;
constexpr unsigned kFdMsgFdf = 0x010000;
constexpr unsigned kFdMsgBrs = 0x020000;
constexpr unsigned kFdMsgEsi = 0x040000;

// canGetChannelData item codes
constexpr int kChDataChannelCap = 1;
constexpr int kChDataDevDescrAscii = 26;

// canCHANNEL_CAP_xxx
constexpr unsigned kCapCanFd = 0x00080000u;
constexpr unsigned kCapVirtual = 0x00010000u;

} // namespace

// ============================================================
//  Shared canlib32.dll (never unload — same rule as PEAK / ZLG)
// ============================================================

CanDeviceKvaser::Api &CanDeviceKvaser::api()
{
    static Api a;
    static bool tried = false;
    if (tried)
        return a;
    tried = true;

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/drivers/kvaser/vendor/canlib32.dll"),
        exeDir + QStringLiteral("/canlib32.dll"),
        QStringLiteral("canlib32"),
        QStringLiteral("canlib32.dll"),
    };

    QLibrary *lib = nullptr;
    for (const QString &c : candidates) {
        auto *tryLib = new QLibrary(c);
        if (tryLib->load()) {
            lib = tryLib;
            OPENBUS_LOG_INFO("Kvaser", "loaded {}", c.toStdString());
            break;
        }
        delete tryLib;
    }
    if (!lib) {
        OPENBUS_LOG_WARN("Kvaser",
            "canlib32.dll not found (install Kvaser drivers / place under "
            "drivers/kvaser/vendor)");
        return a;
    }

    a.InitializeLibrary = reinterpret_cast<decltype(a.InitializeLibrary)>(
        lib->resolve("canInitializeLibrary"));
    a.GetNumberOfChannels = reinterpret_cast<decltype(a.GetNumberOfChannels)>(
        lib->resolve("canGetNumberOfChannels"));
    a.GetChannelData = reinterpret_cast<decltype(a.GetChannelData)>(
        lib->resolve("canGetChannelData"));
    a.OpenChannel = reinterpret_cast<decltype(a.OpenChannel)>(
        lib->resolve("canOpenChannel"));
    a.Close = reinterpret_cast<decltype(a.Close)>(lib->resolve("canClose"));
    a.BusOn = reinterpret_cast<decltype(a.BusOn)>(lib->resolve("canBusOn"));
    a.BusOff = reinterpret_cast<decltype(a.BusOff)>(lib->resolve("canBusOff"));
    a.SetBusParams = reinterpret_cast<decltype(a.SetBusParams)>(
        lib->resolve("canSetBusParams"));
    a.SetBusParamsFd = reinterpret_cast<decltype(a.SetBusParamsFd)>(
        lib->resolve("canSetBusParamsFd"));
    a.Write = reinterpret_cast<decltype(a.Write)>(lib->resolve("canWrite"));
    a.ReadWait = reinterpret_cast<decltype(a.ReadWait)>(lib->resolve("canReadWait"));
    a.Read = reinterpret_cast<decltype(a.Read)>(lib->resolve("canRead"));
    a.GetErrorText = reinterpret_cast<decltype(a.GetErrorText)>(
        lib->resolve("canGetErrorText"));

    a.ok = a.InitializeLibrary && a.GetNumberOfChannels && a.OpenChannel
        && a.Close && a.BusOn && a.BusOff && a.SetBusParams && a.Write
        && (a.ReadWait || a.Read);
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Kvaser", "canlib32.dll missing required exports");
        lib->unload();
        delete lib;
        return a;
    }

    a.InitializeLibrary();
    // Intentionally leak QLibrary so canlib stays loaded for process lifetime
    return a;
}

long CanDeviceKvaser::classicBitrate(int baud)
{
    switch (baud) {
    case 1000000: return kBitrate1M;
    case 500000:  return kBitrate500K;
    case 250000:  return kBitrate250K;
    case 125000:  return kBitrate125K;
    case 100000:  return kBitrate100K;
    case 50000:   return kBitrate50K;
    default:      return kBitrate500K;
    }
}

long CanDeviceKvaser::fdBitrate(int baud)
{
    switch (baud) {
    case 500000:  return kFdBitrate500K80;
    case 1000000: return kFdBitrate1M80;
    case 2000000: return kFdBitrate2M80;
    case 4000000: return kFdBitrate4M80;
    case 8000000: return kFdBitrate8M60;
    default:      return kFdBitrate2M80;
    }
}

QString CanDeviceKvaser::statusText(CanStatus st)
{
    const Api &a = api();
    if (a.GetErrorText) {
        char buf[256] = {};
        if (a.GetErrorText(st, buf, sizeof(buf)) == kOk)
            return QString::fromLocal8Bit(buf);
    }
    return QStringLiteral("status=%1").arg(st);
}

// ============================================================

CanDeviceKvaser::CanDeviceKvaser(int canlibChannel)
    : m_preferredChannel(canlibChannel)
{
}

CanDeviceKvaser::~CanDeviceKvaser()
{
    close();
}

bool CanDeviceKvaser::isAvailable()
{
    return api().ok;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceKvaser::enumerate()
{
    std::vector<DeviceInfo> out;
    const Api &a = api();
    if (!a.ok)
        return out;

    int count = 0;
    if (a.GetNumberOfChannels(&count) != kOk || count <= 0)
        return out;

    for (int ch = 0; ch < count; ++ch) {
        unsigned cap = 0;
        if (a.GetChannelData)
            a.GetChannelData(ch, kChDataChannelCap, &cap, sizeof(cap));

        char descr[256] = {};
        if (a.GetChannelData)
            a.GetChannelData(ch, kChDataDevDescrAscii, descr, sizeof(descr));

        DeviceInfo di;
        di.brand = Brand::Kvaser;
        di.deviceType = ch;          // canOpenChannel(channel)
        di.deviceIndex = ch;
        di.channels = 1;
        di.driverId = QStringLiteral("kvaser");
        di.hasHwTimestamp = true;

        const QString name = QString::fromLocal8Bit(descr).trimmed();
        const bool fd = (cap & kCapCanFd) != 0;
        const bool virt = (cap & kCapVirtual) != 0;
        if (!name.isEmpty())
            di.name = name;
        else
            di.name = QStringLiteral("Kvaser ch%1").arg(ch);
        if (fd)
            di.name += QStringLiteral(" (FD)");
        if (virt)
            di.name += QStringLiteral(" [virtual]");

        out.push_back(di);
    }
    return out;
}

bool CanDeviceKvaser::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const Api &a = api();
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Kvaser", "canlib32.dll not available");
        return false;
    }

    m_channel = channel < 0 ? 0 : channel;

    int canlibCh = -1;
    if (m_preferredChannel >= 0) {
        canlibCh = m_preferredChannel;
    } else {
        const auto attached = enumerate();
        if (devIndex >= 0 && devIndex < int(attached.size()))
            canlibCh = attached[size_t(devIndex)].deviceType;
        else if (!attached.empty())
            canlibCh = attached.front().deviceType;
        else
            canlibCh = 0;
    }

    // Capability probe (before open)
    m_fdCapable = false;
    if (a.GetChannelData) {
        unsigned cap = 0;
        if (a.GetChannelData(canlibCh, kChDataChannelCap, &cap, sizeof(cap)) == kOk)
            m_fdCapable = (cap & kCapCanFd) != 0;
    }

    int flags = kOpenExclusive | kOpenAcceptVirtual;
    m_canFd = canFd && m_fdCapable;
    if (canFd && !m_fdCapable) {
        OPENBUS_LOG_WARN("Kvaser",
            "channel {} is classic-only; opening Classic CAN", canlibCh);
    }
    if (m_canFd)
        flags |= kOpenCanFd;

    const CanHandle h = a.OpenChannel(canlibCh, flags);
    if (h < 0) {
        OPENBUS_LOG_ERROR("Kvaser", "canOpenChannel({}) failed: {}",
                          canlibCh, statusText(h).toStdString());
        return false;
    }
    m_handle = h;

    CanStatus st;
    if (m_canFd) {
        // Arbitration uses canFD_BITRATE_* (not canBITRATE_*); data via SetBusParamsFd
        long arbPreset = kFdBitrate500K80;
        if (arbBaud == 1000000)
            arbPreset = kFdBitrate1M80;
        else if (arbBaud != 500000)
            OPENBUS_LOG_WARN("Kvaser",
                "FD arb {} not in presets; using 500k/80%", arbBaud);

        st = a.SetBusParams(m_handle, arbPreset, 0, 0, 0, 0, 0);
        if (st != kOk) {
            OPENBUS_LOG_ERROR("Kvaser", "canSetBusParams(FD arb) failed: {}",
                              statusText(st).toStdString());
            a.Close(m_handle);
            m_handle = -1;
            return false;
        }
        if (a.SetBusParamsFd) {
            const int data = dataBaud > 0 ? dataBaud : 2000000;
            st = a.SetBusParamsFd(m_handle, fdBitrate(data), 0, 0, 0);
            if (st != kOk) {
                OPENBUS_LOG_ERROR("Kvaser", "canSetBusParamsFd failed: {}",
                                  statusText(st).toStdString());
                a.Close(m_handle);
                m_handle = -1;
                return false;
            }
        }
    } else {
        st = a.SetBusParams(m_handle, classicBitrate(arbBaud), 0, 0, 0, 0, 0);
        if (st != kOk) {
            OPENBUS_LOG_ERROR("Kvaser", "canSetBusParams failed: {}",
                              statusText(st).toStdString());
            a.Close(m_handle);
            m_handle = -1;
            return false;
        }
    }

    st = a.BusOn(m_handle);
    if (st != kOk) {
        OPENBUS_LOG_ERROR("Kvaser", "canBusOn failed: {}",
                          statusText(st).toStdString());
        a.Close(m_handle);
        m_handle = -1;
        return false;
    }

    char descr[256] = {};
    if (a.GetChannelData)
        a.GetChannelData(canlibCh, kChDataDevDescrAscii, descr, sizeof(descr));
    m_deviceName = QString::fromLocal8Bit(descr).trimmed();
    if (m_deviceName.isEmpty())
        m_deviceName = QStringLiteral("Kvaser ch%1").arg(canlibCh);

    m_opened = true;
    OPENBUS_LOG_INFO("Kvaser", "opened {} (ch={}, arb={}, data={}, fd={})",
                     m_deviceName.toStdString(), canlibCh, arbBaud, dataBaud, m_canFd);
    return true;
}

void CanDeviceKvaser::close()
{
    if (!m_opened)
        return;

    const Api &a = api();
    if (m_handle >= 0 && a.ok) {
        if (a.BusOff)
            a.BusOff(m_handle);
        if (a.Close)
            a.Close(m_handle);
    }
    m_handle = -1;
    m_opened = false;
    OPENBUS_LOG_INFO("Kvaser", "closed {}", m_deviceName.toStdString());
}

int CanDeviceKvaser::send(const CanFrame &frame)
{
    if (!m_opened || m_handle < 0)
        return 0;

    const Api &a = api();
    if (!a.Write)
        return 0;

    unsigned flag = frame.extended ? kMsgExt : kMsgStd;
    if (frame.fd || m_canFd) {
        flag |= kFdMsgFdf;
        if (frame.bitrateSwitch)
            flag |= kFdMsgBrs;
        if (frame.errorState)
            flag |= kFdMsgEsi;
    }

    const int len = qMin(64, frame.data.size());
    // canWrite expects non-const void* for some SDK versions
    char buf[64];
    std::memset(buf, 0, sizeof(buf));
    if (len > 0)
        std::memcpy(buf, frame.data.constData(), size_t(len));

    const CanStatus st = a.Write(m_handle, long(frame.id & 0x1FFFFFFFu), buf,
                                 unsigned(len), flag);
    return (st == kOk) ? 1 : 0;
}

int CanDeviceKvaser::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
    if (!m_opened || m_handle < 0)
        return 0;

    const Api &a = api();
    if (!a.ReadWait && !a.Read)
        return 0;

    while (outFrames.size() < 256) {
        long id = 0;
        unsigned dlc = 0;
        unsigned flag = 0;
        unsigned long time = 0;
        char data[64];
        std::memset(data, 0, sizeof(data));

        CanStatus st;
        if (a.ReadWait) {
            const unsigned long to = static_cast<unsigned long>(
                timeoutMs < 0 ? 0 : timeoutMs);
            st = a.ReadWait(m_handle, &id, data, &dlc, &flag, &time, to);
        } else {
            st = a.Read(m_handle, &id, data, &dlc, &flag, &time);
        }

        if (st != kOk) {
            if (st != kErrNomsg)
                OPENBUS_LOG_DEBUG("Kvaser", "read: {}", statusText(st).toStdString());
            break;
        }

        CanFrame frame;
        frame.id = quint32(id) & 0x1FFFFFFFu;
        frame.extended = (flag & kMsgExt) != 0;
        frame.fd = (flag & kFdMsgFdf) != 0;
        frame.bitrateSwitch = (flag & kFdMsgBrs) != 0;
        frame.errorState = (flag & kFdMsgEsi) != 0;
        if (flag & kMsgErrorFrame)
            frame.id |= 0x20000000u; // openbus error-frame marker (same as PEAK path)

        const int nbytes = int(qMin(64u, dlc));
        frame.dlc = CanFrame::lengthToDlc(nbytes);
        frame.data = QByteArray(data, nbytes);
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;
        // Hardware time is ms-scale in canReadWait; CanDeviceManager fills ns

        outFrames.push_back(std::move(frame));
        timeoutMs = 0; // drain remaining without blocking
    }

    return static_cast<int>(outFrames.size());
}

int CanDeviceKvaser::pendingCount() const
{
    return m_opened ? 1 : 0;
}

QString CanDeviceKvaser::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("Kvaser CAN") : m_deviceName;
}
