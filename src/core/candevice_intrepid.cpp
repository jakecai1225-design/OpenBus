#include "candevice_intrepid.h"
#include "logging.h"

#include <QCoreApplication>
#include <QLibrary>

#include <cstring>

namespace {

#pragma pack(push, 1)
struct NeoMessageCan {
    uint32_t statusBitfield[4] = {};
    uint64_t timestamp = 0;
    uint64_t reservedTimestamp = 0;
    const uint8_t *data = nullptr;
    size_t length = 0;
    uint32_t arbid = 0;
    uint16_t netid = 0;
    uint8_t type = 0;
    uint8_t dlcOnWire = 0;
    uint16_t description = 0;
    uint16_t messageType = 0;
    uint8_t reserved1[12] = {};
};
#pragma pack(pop)

static_assert(sizeof(NeoMessageCan) == (56 + sizeof(void *) + sizeof(size_t)),
              "neomessage_can_t layout must match libicsneo C ABI");

constexpr uint16_t kNetIdInvalid = 0xffff;
constexpr uint8_t kNetworkTypeCan = 2;
constexpr uint16_t kMessageTypeFrame = 0;

constexpr uint32_t kStatusTx = 1u << 1;
constexpr uint32_t kStatusExtended = 1u << 2;
constexpr uint32_t kStatusRemote = 1u << 3;
constexpr uint32_t kStatusErrorFrame = 1u << 17; // statusBitfield[1]
constexpr uint32_t kStatusFdFdf = 1u << 3;       // statusBitfield[2]
constexpr uint32_t kStatusFdBrs = 1u << 4;
constexpr uint32_t kStatusFdEsi = 1u << 0;

// libicsneo DeviceType::Enum values used for FD tagging (ValueCAN 4 / FIRE2+)
constexpr uint32_t kTypeVcan41 = 7;
constexpr uint32_t kTypeVcan42El = 10;
constexpr uint32_t kTypeFire3 = 15;
constexpr uint32_t kTypeVcan4Ind = 18;
constexpr uint32_t kTypeRed2 = 20;
constexpr uint32_t kTypeVcan44 = 0x100000;
constexpr uint32_t kTypeVcan42 = 0x200000;
constexpr uint32_t kTypeFire2 = 0x4000000;

QString serialOf(const char serial[7])
{
    int n = 0;
    while (n < 6 && serial[n])
        ++n;
    return QString::fromLatin1(serial, n);
}

} // namespace

CanDeviceIntrepid::Api &CanDeviceIntrepid::api()
{
    static Api a;
    static bool tried = false;
    if (tried)
        return a;
    tried = true;

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
#ifdef _WIN32
        exeDir + QStringLiteral("/drivers/intrepid/vendor/icsneoc.dll"),
        exeDir + QStringLiteral("/icsneoc.dll"),
        QStringLiteral("icsneoc"),
        QStringLiteral("icsneoc.dll"),
#else
        exeDir + QStringLiteral("/drivers/intrepid/vendor/libicsneoc.so"),
        exeDir + QStringLiteral("/libicsneoc.so"),
        QStringLiteral("icsneoc"),
        QStringLiteral("libicsneoc.so"),
#endif
    };

    QLibrary *lib = nullptr;
    for (const QString &c : candidates) {
        auto *tryLib = new QLibrary(c);
        if (tryLib->load()) {
            lib = tryLib;
            OPENBUS_LOG_INFO("Intrepid", "loaded {}", c.toStdString());
            break;
        }
        delete tryLib;
    }
    if (!lib) {
        OPENBUS_LOG_WARN("Intrepid",
            "icsneoc not found (build/install libicsneo or place under "
            "drivers/intrepid/vendor)");
        return a;
    }

    a.FindAllDevices = reinterpret_cast<decltype(a.FindAllDevices)>(
        lib->resolve("icsneo_findAllDevices"));
    a.FreeUnconnected = reinterpret_cast<decltype(a.FreeUnconnected)>(
        lib->resolve("icsneo_freeUnconnectedDevices"));
    a.OpenDevice = reinterpret_cast<decltype(a.OpenDevice)>(lib->resolve("icsneo_openDevice"));
    a.CloseDevice = reinterpret_cast<decltype(a.CloseDevice)>(lib->resolve("icsneo_closeDevice"));
    a.GoOnline = reinterpret_cast<decltype(a.GoOnline)>(lib->resolve("icsneo_goOnline"));
    a.GoOffline = reinterpret_cast<decltype(a.GoOffline)>(lib->resolve("icsneo_goOffline"));
    a.EnablePolling = reinterpret_cast<decltype(a.EnablePolling)>(
        lib->resolve("icsneo_enableMessagePolling"));
    a.DisablePolling = reinterpret_cast<decltype(a.DisablePolling)>(
        lib->resolve("icsneo_disableMessagePolling"));
    a.GetMessages = reinterpret_cast<decltype(a.GetMessages)>(lib->resolve("icsneo_getMessages"));
    a.SetPollingLimit = reinterpret_cast<decltype(a.SetPollingLimit)>(
        lib->resolve("icsneo_setPollingMessageLimit"));
    a.GetNetworkByNumber = reinterpret_cast<decltype(a.GetNetworkByNumber)>(
        lib->resolve("icsneo_getNetworkByNumber"));
    a.GetProductName = reinterpret_cast<decltype(a.GetProductName)>(
        lib->resolve("icsneo_getProductName"));
    a.DescribeDevice = reinterpret_cast<decltype(a.DescribeDevice)>(
        lib->resolve("icsneo_describeDevice"));
    a.SettingsRefresh = reinterpret_cast<decltype(a.SettingsRefresh)>(
        lib->resolve("icsneo_settingsRefresh"));
    a.SettingsApplyTemporary = reinterpret_cast<decltype(a.SettingsApplyTemporary)>(
        lib->resolve("icsneo_settingsApplyTemporary"));
    a.SetBaudrate = reinterpret_cast<decltype(a.SetBaudrate)>(lib->resolve("icsneo_setBaudrate"));
    a.SetFDBaudrate = reinterpret_cast<decltype(a.SetFDBaudrate)>(
        lib->resolve("icsneo_setFDBaudrate"));
    a.Transmit = reinterpret_cast<decltype(a.Transmit)>(lib->resolve("icsneo_transmit"));

    a.ok = a.FindAllDevices && a.OpenDevice && a.CloseDevice && a.GoOnline && a.GoOffline
        && a.EnablePolling && a.GetMessages && a.GetNetworkByNumber && a.SetBaudrate
        && a.Transmit && a.SettingsRefresh && a.SettingsApplyTemporary;
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Intrepid", "icsneoc missing required exports");
        lib->unload();
        delete lib;
        return a;
    }
    return a;
}

bool CanDeviceIntrepid::looksFdCapable(uint32_t deviceType)
{
    switch (deviceType) {
    case kTypeVcan41:
    case kTypeVcan42El:
    case kTypeFire3:
    case kTypeVcan4Ind:
    case kTypeRed2:
    case kTypeVcan44:
    case kTypeVcan42:
    case kTypeFire2:
        return true;
    default:
        return false;
    }
}

int CanDeviceIntrepid::countCanNetworks(const NeoDevice &dev)
{
    const Api &a = api();
    if (!a.GetNetworkByNumber)
        return 1;
    int n = 0;
    for (unsigned i = 1; i <= 16; ++i) {
        const uint16_t id = a.GetNetworkByNumber(&dev, kNetworkTypeCan, i);
        if (id == kNetIdInvalid || id == 0)
            break;
        ++n;
    }
    return n > 0 ? n : 1;
}

QString CanDeviceIntrepid::productLabel(const NeoDevice &dev)
{
    const Api &a = api();
    char buf[64] = {};
    size_t len = sizeof(buf) - 1;
    if (a.DescribeDevice && a.DescribeDevice(&dev, buf, &len) && buf[0])
        return QString::fromLocal8Bit(buf);
    len = sizeof(buf) - 1;
    if (a.GetProductName && a.GetProductName(&dev, buf, &len) && buf[0]) {
        const QString serial = serialOf(dev.serial);
        if (!serial.isEmpty())
            return QString::fromLocal8Bit(buf) + QLatin1Char(' ') + serial;
        return QString::fromLocal8Bit(buf);
    }
    const QString serial = serialOf(dev.serial);
    if (!serial.isEmpty())
        return QStringLiteral("Intrepid %1").arg(serial);
    return QStringLiteral("Intrepid device");
}

CanDeviceIntrepid::CanDeviceIntrepid(int deviceIndex)
    : m_preferredIndex(deviceIndex)
{
}

CanDeviceIntrepid::~CanDeviceIntrepid()
{
    close();
}

bool CanDeviceIntrepid::isAvailable()
{
    return api().ok;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceIntrepid::enumerate()
{
    std::vector<DeviceInfo> out;
    const Api &a = api();
    if (!a.ok)
        return out;

    size_t count = 0;
    a.FindAllDevices(nullptr, &count);
    if (count == 0)
        return out;

    std::vector<NeoDevice> devices(count);
    a.FindAllDevices(devices.data(), &count);
    devices.resize(count);

    for (size_t i = 0; i < devices.size(); ++i) {
        DeviceInfo di;
        di.brand = Brand::Intrepid;
        di.deviceType = int(i);
        di.deviceIndex = int(i);
        di.channels = countCanNetworks(devices[i]);
        di.driverId = QStringLiteral("intrepid");
        di.hasHwTimestamp = true;
        QString label = productLabel(devices[i]);
        if (looksFdCapable(devices[i].type))
            label += QStringLiteral(" (FD)");
        di.name = label;
        out.push_back(di);
    }

    if (a.FreeUnconnected)
        a.FreeUnconnected();
    return out;
}

bool CanDeviceIntrepid::resolveDevice(int preferIndex, NeoDevice *out)
{
    const Api &a = api();
    if (!a.ok || !out)
        return false;

    size_t count = 0;
    a.FindAllDevices(nullptr, &count);
    if (count == 0)
        return false;

    std::vector<NeoDevice> devices(count);
    a.FindAllDevices(devices.data(), &count);
    devices.resize(count);

    int pick = -1;
    if (!m_preferredSerial.isEmpty()) {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (serialOf(devices[i].serial) == m_preferredSerial) {
                pick = int(i);
                break;
            }
        }
    }
    if (pick < 0) {
        if (preferIndex >= 0 && preferIndex < int(devices.size()))
            pick = preferIndex;
        else if (m_preferredIndex >= 0 && m_preferredIndex < int(devices.size()))
            pick = m_preferredIndex;
        else if (!devices.empty())
            pick = 0;
    }
    if (pick < 0)
        return false;

    *out = devices[size_t(pick)];
    m_preferredIndex = pick;
    m_preferredSerial = serialOf(out->serial);
    return true;
}

bool CanDeviceIntrepid::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const Api &a = api();
    if (!a.ok) {
        OPENBUS_LOG_ERROR("Intrepid", "icsneoc unavailable");
        return false;
    }

    NeoDevice device;
    if (!resolveDevice(devIndex, &device)) {
        OPENBUS_LOG_ERROR("Intrepid", "no Intrepid device found");
        return false;
    }

    if (!a.OpenDevice(&device)) {
        OPENBUS_LOG_ERROR("Intrepid", "icsneo_openDevice failed");
        return false;
    }

    m_channel = channel < 0 ? 0 : channel;
    const unsigned netNumber = unsigned(m_channel) + 1u;
    m_netId = a.GetNetworkByNumber(&device, kNetworkTypeCan, netNumber);
    if (m_netId == kNetIdInvalid || m_netId == 0) {
        OPENBUS_LOG_ERROR("Intrepid", "no CAN network #{} on device", netNumber);
        a.CloseDevice(&device);
        return false;
    }

    if (a.SettingsRefresh)
        a.SettingsRefresh(&device);

    const int arb = arbBaud > 0 ? arbBaud : 500000;
    if (!a.SetBaudrate(&device, m_netId, arb)) {
        OPENBUS_LOG_ERROR("Intrepid", "setBaudrate({}) failed", arb);
        a.CloseDevice(&device);
        return false;
    }

    m_canFd = canFd;
    if (canFd && a.SetFDBaudrate) {
        const int data = dataBaud > 0 ? dataBaud : 2000000;
        if (!a.SetFDBaudrate(&device, m_netId, data)) {
            OPENBUS_LOG_WARN("Intrepid",
                "setFDBaudrate({}) failed — continuing classic on this net", data);
            m_canFd = false;
        }
    }

    if (!a.SettingsApplyTemporary(&device)) {
        OPENBUS_LOG_ERROR("Intrepid", "settingsApplyTemporary failed");
        a.CloseDevice(&device);
        return false;
    }

    if (a.SetPollingLimit)
        a.SetPollingLimit(&device, 50000);
    if (a.EnablePolling && !a.EnablePolling(&device)) {
        OPENBUS_LOG_ERROR("Intrepid", "enableMessagePolling failed");
        a.CloseDevice(&device);
        return false;
    }
    if (!a.GoOnline(&device)) {
        OPENBUS_LOG_ERROR("Intrepid", "goOnline failed");
        a.CloseDevice(&device);
        return false;
    }

    m_device = device;
    m_deviceName = productLabel(device);
    m_opened = true;
    OPENBUS_LOG_INFO("Intrepid",
        "opened {} ch={} netid={} arb={} data={} fd={}",
        m_deviceName.toStdString(), m_channel, m_netId, arb, dataBaud, m_canFd);
    return true;
}

void CanDeviceIntrepid::close()
{
    if (!m_opened)
        return;
    const Api &a = api();
    if (a.ok && m_device.device) {
        if (a.GoOffline)
            a.GoOffline(&m_device);
        if (a.DisablePolling)
            a.DisablePolling(&m_device);
        if (a.CloseDevice)
            a.CloseDevice(&m_device);
    }
    OPENBUS_LOG_INFO("Intrepid", "closed {}", m_deviceName.toStdString());
    m_device = NeoDevice{};
    m_netId = 0;
    m_opened = false;
}

int CanDeviceIntrepid::send(const CanFrame &frame)
{
    if (!m_opened || !m_device.device)
        return 0;
    const Api &a = api();
    if (!a.Transmit)
        return 0;

    const int maxLen = m_canFd ? 64 : 8;
    const int nbytes = qMin(maxLen, frame.data.size());
    uint8_t payload[64] = {};
    if (nbytes > 0)
        std::memcpy(payload, frame.data.constData(), size_t(nbytes));

    NeoMessageCan msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.data = payload;
    msg.length = size_t(nbytes);
    msg.arbid = frame.id & 0x1FFFFFFFu;
    msg.netid = m_netId;
    msg.type = kNetworkTypeCan;
    msg.messageType = kMessageTypeFrame;
    msg.dlcOnWire = CanFrame::lengthToDlc(nbytes);
    if (frame.extended)
        msg.statusBitfield[0] |= kStatusExtended;
    if (m_canFd && (frame.fd || nbytes > 8 || frame.bitrateSwitch)) {
        msg.statusBitfield[2] |= kStatusFdFdf;
        if (frame.bitrateSwitch)
            msg.statusBitfield[2] |= kStatusFdBrs;
        if (frame.errorState)
            msg.statusBitfield[2] |= kStatusFdEsi;
    }

    return a.Transmit(&m_device, &msg) ? 1 : 0;
}

int CanDeviceIntrepid::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
    if (!m_opened || !m_device.device)
        return 0;
    const Api &a = api();
    if (!a.GetMessages)
        return 0;

    const uint64_t timeout = timeoutMs < 0 ? 1000 : uint64_t(qMax(0, timeoutMs));
    NeoMessageCan buf[256];
    size_t items = 256;
    if (!a.GetMessages(&m_device, buf, &items, timeout) || items == 0)
        return 0;

    outFrames.reserve(items);
    for (size_t i = 0; i < items; ++i) {
        const NeoMessageCan &m = buf[i];
        if (m.type != kNetworkTypeCan)
            continue;
        if (m.netid != m_netId)
            continue;
        if (m.statusBitfield[1] & kStatusErrorFrame)
            continue;
        if (m.messageType != kMessageTypeFrame && m.messageType != 0)
            continue;

        CanFrame f;
        f.id = m.arbid;
        f.extended = (m.statusBitfield[0] & kStatusExtended) != 0;
        f.fd = (m.statusBitfield[2] & kStatusFdFdf) != 0;
        f.bitrateSwitch = (m.statusBitfield[2] & kStatusFdBrs) != 0;
        f.errorState = (m.statusBitfield[2] & kStatusFdEsi) != 0;
        f.direction = (m.statusBitfield[0] & kStatusTx) ? CanFrame::Tx : CanFrame::Rx;
        f.channel = static_cast<quint8>(m_channel + 1);
        f.timestampNs = m.timestamp;
        if (m.timestamp)
            f.timestamp = double(m.timestamp) * 1e-9;

        const int maxLen = f.fd ? 64 : 8;
        int nbytes = int(m.length);
        if (nbytes < 0)
            nbytes = 0;
        if (nbytes > maxLen)
            nbytes = maxLen;
        if ((m.statusBitfield[0] & kStatusRemote) == 0 && m.data && nbytes > 0)
            f.data = QByteArray(reinterpret_cast<const char *>(m.data), nbytes);
        f.dlc = CanFrame::lengthToDlc(nbytes);
        outFrames.push_back(f);
    }
    return int(outFrames.size());
}

int CanDeviceIntrepid::pendingCount() const
{
    if (!m_opened || !m_device.device)
        return 0;
    const Api &a = api();
    if (!a.GetMessages)
        return 0;
    size_t items = 0;
    if (!a.GetMessages(&m_device, nullptr, &items, 0))
        return 0;
    return int(items);
}

QString CanDeviceIntrepid::deviceName() const
{
    if (!m_deviceName.isEmpty())
        return m_deviceName;
    return QStringLiteral("Intrepid");
}
