#include "candevice_peak.h"
#include "logging.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLibrary>
#include <QThread>

#include <algorithm>
#include <cstring>

namespace {

constexpr unsigned int kOk = 0x00000u;
constexpr unsigned int kQEmpty = 0x00020u;
constexpr unsigned int kHwInUse = 0x00400u;

constexpr unsigned char kMsgStandard = 0x00;
constexpr unsigned char kMsgRtr = 0x01;
constexpr unsigned char kMsgExtended = 0x02;
constexpr unsigned char kMsgFd = 0x04;
constexpr unsigned char kMsgBrs = 0x08;
constexpr unsigned char kMsgEsi = 0x10;

constexpr unsigned char kParamHardwareName = 0x0Eu;
constexpr unsigned char kParamChannelCondition = 0x0Du;
constexpr unsigned char kParamChannelFeatures = 0x16u;
constexpr unsigned char kParamBitrateAdapting = 0x17u;
constexpr unsigned char kParamAttachedCount = 0x2Au;
constexpr unsigned char kParamAttachedChannels = 0x2Bu;

constexpr unsigned short kNoneBus = 0x00;
constexpr unsigned int kFeatureFd = 0x01u;
constexpr unsigned int kChannelAvailable = 0x01u;
constexpr unsigned int kChannelOccupied = 0x02u;
constexpr unsigned int kChannelPcanView = kChannelAvailable | kChannelOccupied;

constexpr unsigned short kBaud1M = 0x0014;
constexpr unsigned short kBaud800K = 0x0016;
constexpr unsigned short kBaud500K = 0x001C;
constexpr unsigned short kBaud250K = 0x011C;
constexpr unsigned short kBaud125K = 0x031C;
constexpr unsigned short kBaud100K = 0x432F;
constexpr unsigned short kBaud50K = 0x472F;

} // namespace

// ============================================================
//  Shared PCANBasic.dll (never unload — same rule as ZLG / Candle)
// ============================================================

CanDevicePEAK::Api &CanDevicePEAK::api()
{
    static Api a;
    static bool tried = false;
    if (tried)
        return a;
    tried = true;

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/drivers/peak/vendor/PCANBasic.dll"),
        exeDir + QStringLiteral("/PCANBasic.dll"),
        QStringLiteral("PCANBasic"),
        QStringLiteral("PCANBasic.dll"),
    };

    QLibrary *lib = nullptr;
    for (const QString &c : candidates) {
        auto *tryLib = new QLibrary(c);
        if (tryLib->load()) {
            lib = tryLib;
            OPENBUS_LOG_INFO("PEAK", "loaded {}", c.toStdString());
            break;
        }
        delete tryLib;
    }
    if (!lib) {
        OPENBUS_LOG_WARN("PEAK",
            "PCANBasic.dll not found (place under drivers/peak/vendor or install PEAK drivers)");
        return a;
    }

    a.Initialize = reinterpret_cast<decltype(a.Initialize)>(lib->resolve("CAN_Initialize"));
    a.InitializeFD = reinterpret_cast<decltype(a.InitializeFD)>(lib->resolve("CAN_InitializeFD"));
    a.Uninitialize = reinterpret_cast<decltype(a.Uninitialize)>(lib->resolve("CAN_Uninitialize"));
    a.Read = reinterpret_cast<decltype(a.Read)>(lib->resolve("CAN_Read"));
    a.ReadFD = reinterpret_cast<decltype(a.ReadFD)>(lib->resolve("CAN_ReadFD"));
    a.Write = reinterpret_cast<decltype(a.Write)>(lib->resolve("CAN_Write"));
    a.WriteFD = reinterpret_cast<decltype(a.WriteFD)>(lib->resolve("CAN_WriteFD"));
    a.GetValue = reinterpret_cast<decltype(a.GetValue)>(lib->resolve("CAN_GetValue"));
    a.SetValue = reinterpret_cast<decltype(a.SetValue)>(lib->resolve("CAN_SetValue"));
    a.GetStatus = reinterpret_cast<decltype(a.GetStatus)>(lib->resolve("CAN_GetStatus"));

    a.ok = a.Initialize && a.Uninitialize && a.Read && a.Write && a.GetValue;
    if (!a.ok) {
        OPENBUS_LOG_ERROR("PEAK", "PCANBasic.dll missing required exports");
        lib->unload();
        delete lib;
    }
    // On success: intentionally leak QLibrary so DLL stays loaded
    return a;
}

CanDevicePEAK::TPCANBaudrate CanDevicePEAK::baudToBtr(int bitrate)
{
    switch (bitrate) {
    case 1000000: return kBaud1M;
    case 800000:  return kBaud800K;
    case 500000:  return kBaud500K;
    case 250000:  return kBaud250K;
    case 125000:  return kBaud125K;
    case 100000:  return kBaud100K;
    case 50000:   return kBaud50K;
    default:      return kBaud500K;
    }
}

QByteArray CanDevicePEAK::buildFdBitrate(int arbBaud, int dataBaud)
{
    // 80 MHz clock presets matching PEAK examples (nom ≈ 87.5% sample point)
    struct Preset { int arb; int data; const char *str; };
    static const Preset kPresets[] = {
        { 500000, 2000000,
          "f_clock=80000000,nom_brp=10,nom_tseg1=12,nom_tseg2=3,nom_sjw=1,"
          "data_brp=4,data_tseg1=7,data_tseg2=2,data_sjw=1" },
        { 500000, 4000000,
          "f_clock=80000000,nom_brp=10,nom_tseg1=12,nom_tseg2=3,nom_sjw=1,"
          "data_brp=2,data_tseg1=7,data_tseg2=2,data_sjw=1" },
        { 1000000, 2000000,
          "f_clock=80000000,nom_brp=5,nom_tseg1=12,nom_tseg2=3,nom_sjw=1,"
          "data_brp=4,data_tseg1=7,data_tseg2=2,data_sjw=1" },
        { 1000000, 5000000,
          "f_clock=80000000,nom_brp=5,nom_tseg1=12,nom_tseg2=3,nom_sjw=1,"
          "data_brp=2,data_tseg1=5,data_tseg2=2,data_sjw=1" },
        { 250000, 2000000,
          "f_clock=80000000,nom_brp=20,nom_tseg1=12,nom_tseg2=3,nom_sjw=1,"
          "data_brp=4,data_tseg1=7,data_tseg2=2,data_sjw=1" },
    };
    const int d = dataBaud > 0 ? dataBaud : 2000000;
    for (const auto &p : kPresets) {
        if (p.arb == arbBaud && p.data == d)
            return QByteArray(p.str);
    }
    // Fallback: 500k / 2M
    return QByteArray(kPresets[0].str);
}

quint64 CanDevicePEAK::classicTsToNs(const TPCANTimestamp &ts)
{
    const quint64 totalUs =
        quint64(ts.millis) * 1000ull
        + quint64(ts.micros)
        + quint64(ts.millis_overflow) * 0x100000000ull * 1000ull;
    return totalUs * 1000ull;
}

// ============================================================

CanDevicePEAK::CanDevicePEAK(int channelHandle)
    : m_preferredHandle(channelHandle)
{
}

CanDevicePEAK::~CanDevicePEAK()
{
    close();
}

bool CanDevicePEAK::isAvailable()
{
    return api().ok;
}

std::vector<ICanDevice::DeviceInfo> CanDevicePEAK::enumerate()
{
    std::vector<DeviceInfo> out;
    const Api &a = api();
    if (!a.ok)
        return out;

    unsigned int count = 0;
    if (a.GetValue(kNoneBus, kParamAttachedCount, &count, sizeof(count)) != kOk || count == 0)
        return out;

    std::vector<TPCANChannelInformation> infos(count);
    std::memset(infos.data(), 0, infos.size() * sizeof(TPCANChannelInformation));
    if (a.GetValue(kNoneBus, kParamAttachedChannels, infos.data(),
                   unsigned(infos.size() * sizeof(TPCANChannelInformation))) != kOk) {
        OPENBUS_LOG_WARN("PEAK", "PCAN_ATTACHED_CHANNELS failed");
        return out;
    }

    int index = 0;
    for (const auto &ch : infos) {
        // Skip unavailable; keep occupied / PCAN-View (can still attach with BITRATE_ADAPTING)
        if (ch.channel_condition == 0)
            continue;

        DeviceInfo di;
        di.brand = Brand::PEAK;
        di.deviceType = int(ch.channel_handle); // open uses this handle
        di.deviceIndex = index++;
        di.channels = 1;
        di.driverId = QStringLiteral("peak");
        di.hasHwTimestamp = true;
        const QString name = QString::fromLatin1(ch.device_name).trimmed();
        const bool fd = (ch.device_features & kFeatureFd) != 0;
        di.name = name.isEmpty()
            ? QStringLiteral("PCAN 0x%1").arg(ch.channel_handle, 0, 16)
            : name;
        if (fd)
            di.name += QStringLiteral(" (FD)");
        if (ch.channel_condition & kChannelOccupied)
            di.name += QStringLiteral(" [in use]");
        out.push_back(di);
    }
    return out;
}

bool CanDevicePEAK::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const Api &a = api();
    if (!a.ok) {
        OPENBUS_LOG_ERROR("PEAK", "PCANBasic.dll not available");
        return false;
    }

    m_channel = channel < 0 ? 0 : channel;

    // Resolve handle: preferred (deviceType) → attached list by index → USBBUS1
    TPCANHandle handle = 0;
    if (m_preferredHandle > 0) {
        handle = static_cast<TPCANHandle>(m_preferredHandle);
    } else {
        const auto attached = enumerate();
        if (devIndex >= 0 && devIndex < int(attached.size()))
            handle = static_cast<TPCANHandle>(attached[size_t(devIndex)].deviceType);
        else if (!attached.empty())
            handle = static_cast<TPCANHandle>(attached.front().deviceType);
        else
            handle = PCAN_USBBUS1;
    }
    m_handle = handle;

    // Detect FD capability
    unsigned int features = 0;
    m_fdCapable = false;
    if (a.GetValue(m_handle, kParamChannelFeatures, &features, sizeof(features)) == kOk)
        m_fdCapable = (features & kFeatureFd) != 0;

    // Allow share with PCAN-View when channel is occupied-but-available
    if (a.SetValue) {
        unsigned int on = 1;
        a.SetValue(m_handle, kParamBitrateAdapting, &on, sizeof(on));
    }

    TPCANStatus st;
    m_canFd = canFd && m_fdCapable && a.InitializeFD;
    if (canFd && !m_fdCapable) {
        OPENBUS_LOG_WARN("PEAK",
            "channel 0x{:X} is classic-only (PCAN-USB); opening Classic CAN",
            int(m_handle));
    }

    if (m_canFd) {
        const QByteArray br = buildFdBitrate(arbBaud, dataBaud);
        st = a.InitializeFD(m_handle, br.constData());
    } else {
        st = a.Initialize(m_handle, baudToBtr(arbBaud), 0, 0, 0);
    }

    if (st != kOk) {
        OPENBUS_LOG_ERROR("PEAK",
            "CAN_Initialize{} failed: handle=0x{:X} status=0x{:X}{}",
            m_canFd ? "FD" : "", int(m_handle), st,
            st == kHwInUse ? " (channel in use — close PCAN-View?)" : "");
        return false;
    }

    char nameBuf[64] = {};
    if (a.GetValue(m_handle, kParamHardwareName, nameBuf, sizeof(nameBuf)) == kOk)
        m_deviceName = QString::fromLatin1(nameBuf).trimmed();
    if (m_deviceName.isEmpty())
        m_deviceName = QStringLiteral("PCAN 0x%1").arg(m_handle, 0, 16);

    m_opened = true;
    OPENBUS_LOG_INFO("PEAK", "opened {} (handle=0x{:X}, arb={}, fd={})",
                     m_deviceName.toStdString(), int(m_handle), arbBaud, m_canFd);
    return true;
}

void CanDevicePEAK::close()
{
    if (!m_opened)
        return;
    if (api().Uninitialize)
        api().Uninitialize(m_handle);
    m_opened = false;
    m_handle = 0;
    OPENBUS_LOG_INFO("PEAK", "closed {}", m_deviceName.toStdString());
}

int CanDevicePEAK::send(const CanFrame &frame)
{
    if (!m_opened)
        return 0;
    const Api &a = api();

    if (m_canFd && a.WriteFD) {
        TPCANMsgFD msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.ID = frame.id;
        msg.MSGTYPE = (frame.extended ? kMsgExtended : kMsgStandard) | kMsgFd
                    | (frame.bitrateSwitch ? kMsgBrs : 0)
                    | (frame.errorState ? kMsgEsi : 0);
        msg.DLC = frame.dlc;
        const int len = std::min(64, CanFrame::dlcToLength(frame.dlc));
        std::memcpy(msg.DATA, frame.data.constData(), size_t(len));
        return a.WriteFD(m_handle, &msg) == kOk ? 1 : 0;
    }

    if (!a.Write)
        return 0;
    TPCANMsg msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.ID = frame.id;
    msg.MSGTYPE = frame.extended ? kMsgExtended : kMsgStandard;
    const int len = int(std::min<qsizetype>(frame.data.size(), 8));
    msg.LEN = static_cast<unsigned char>(len);
    std::memcpy(msg.DATA, frame.data.constData(), size_t(len));
    return a.Write(m_handle, &msg) == kOk ? 1 : 0;
}

int CanDevicePEAK::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened)
        return 0;
    const Api &a = api();
    outFrames.clear();

    QElapsedTimer timer;
    timer.start();
    int got = 0;

    forever {
        CanFrame frame;
        frame.channel = quint8(m_channel + 1);
        frame.direction = CanFrame::Rx;

        TPCANStatus st;
        if (m_canFd && a.ReadFD) {
            TPCANMsgFD msg;
            unsigned long long tsFd = 0;
            std::memset(&msg, 0, sizeof(msg));
            st = a.ReadFD(m_handle, &msg, &tsFd);
            if (st == kOk) {
                frame.id = msg.ID & 0x1FFFFFFFu;
                frame.extended = (msg.MSGTYPE & kMsgExtended) != 0;
                frame.fd = true;
                frame.bitrateSwitch = (msg.MSGTYPE & kMsgBrs) != 0;
                frame.errorState = (msg.MSGTYPE & kMsgEsi) != 0;
                frame.dlc = msg.DLC;
                const int len = std::min(64, CanFrame::dlcToLength(msg.DLC));
                frame.data = QByteArray(reinterpret_cast<const char *>(msg.DATA), len);
                // PCAN FD timestamp is microseconds since start → ns
                if (tsFd)
                    frame.timestampNs = tsFd * 1000ull;
            }
        } else if (a.Read) {
            TPCANMsg msg;
            TPCANTimestamp ts;
            std::memset(&msg, 0, sizeof(msg));
            std::memset(&ts, 0, sizeof(ts));
            st = a.Read(m_handle, &msg, &ts);
            if (st == kOk) {
                frame.id = msg.ID & 0x1FFFFFFFu;
                frame.extended = (msg.MSGTYPE & kMsgExtended) != 0;
                frame.fd = false;
                frame.dlc = msg.LEN;
                frame.data = QByteArray(reinterpret_cast<const char *>(msg.DATA), msg.LEN);
                frame.timestampNs = classicTsToNs(ts);
            }
        } else {
            break;
        }

        if (st == kOk) {
            outFrames.push_back(std::move(frame));
            ++got;
            if (got >= 256)
                break;
            continue;
        }
        if (st != kQEmpty)
            OPENBUS_LOG_DEBUG("PEAK", "read status=0x{:X}", st);

        if (timeoutMs <= 0 || timer.elapsed() >= timeoutMs)
            break;
        QThread::msleep(1);
    }
    return got;
}

int CanDevicePEAK::pendingCount() const
{
    return m_opened ? 1 : 0;
}

QString CanDevicePEAK::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("PEAK PCAN") : m_deviceName;
}
