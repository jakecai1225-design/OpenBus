#include "candevice_tongxing.h"
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

constexpr unsigned kOk = 0;
constexpr unsigned char kPropTx = 0x01;
constexpr unsigned char kPropExt = 0x04;
constexpr unsigned char kPropErr = 0x80;
constexpr unsigned char kFdEdl = 0x01;
constexpr unsigned char kFdBrs = 0x02;
constexpr unsigned char kFdEsi = 0x04;
constexpr unsigned char kRxTx = 1; // FIFO returns RX and TX
constexpr int kCtrlIso = 1;
constexpr int kCtrlNormal = 0;

#pragma pack(push, 1)
struct TlibCan {
    unsigned char idxChn;
    unsigned char properties;
    unsigned char dlc;
    unsigned char reserved;
    std::int32_t identifier;
    std::int64_t timeUs;
    unsigned char data[8];
};
static_assert(sizeof(TlibCan) == 24, "TLIBCAN layout");

struct TlibCanFd {
    unsigned char idxChn;
    unsigned char properties;
    unsigned char dlc;
    unsigned char fdProperties;
    std::int32_t identifier;
    std::int64_t timeUs;
    unsigned char data[64];
};
static_assert(sizeof(TlibCanFd) == 80, "TLIBCANFD layout");
#pragma pack(pop)

QString fromCStr(const char *s)
{
    if (!s || !s[0])
        return {};
    return QString::fromLocal8Bit(s).trimmed();
}

} // namespace

CanDeviceTongXing::Api &CanDeviceTongXing::api()
{
    static Api a;
    static bool tried = false;
    if (tried)
        return a;
    tried = true;

#ifndef _WIN32
    OPENBUS_LOG_WARN("TongXing", "libTSCAN is Windows-only in this build");
    return a;
#else
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        exeDir + QStringLiteral("/drivers/tongxing/vendor/libTSCAN.dll"),
        exeDir + QStringLiteral("/libTSCAN.dll"),
        QStringLiteral("libTSCAN"),
        QStringLiteral("libTSCAN.dll"),
    };

    QLibrary *lib = nullptr;
    for (const QString &c : candidates) {
        auto *tryLib = new QLibrary(c);
        if (tryLib->load()) {
            lib = tryLib;
            OPENBUS_LOG_INFO("TongXing", "loaded {}", c.toStdString());
            break;
        }
        delete tryLib;
    }
    if (!lib) {
        OPENBUS_LOG_WARN("TongXing",
            "libTSCAN.dll not found (install TOSUN runtime or place "
            "libTSCAN.dll + libTSH.dll under drivers/tongxing/vendor)");
        return a;
    }

    a.Initialize = reinterpret_cast<decltype(a.Initialize)>(lib->resolve("initialize_lib_tscan"));
    a.Finalize = reinterpret_cast<decltype(a.Finalize)>(lib->resolve("finalize_lib_tscan"));
    a.ScanDevices = reinterpret_cast<decltype(a.ScanDevices)>(lib->resolve("tscan_scan_devices"));
    a.GetDeviceInfo = reinterpret_cast<decltype(a.GetDeviceInfo)>(
        lib->resolve("tscan_get_device_info"));
    a.Connect = reinterpret_cast<decltype(a.Connect)>(lib->resolve("tscan_connect"));
    a.Disconnect = reinterpret_cast<decltype(a.Disconnect)>(
        lib->resolve("tscan_disconnect_by_handle"));
    a.GetCanChannelCount = reinterpret_cast<decltype(a.GetCanChannelCount)>(
        lib->resolve("tscan_get_can_channel_count"));
    a.ConfigCan = reinterpret_cast<decltype(a.ConfigCan)>(
        lib->resolve("tscan_config_can_by_baudrate"));
    a.ConfigCanFd = reinterpret_cast<decltype(a.ConfigCanFd)>(
        lib->resolve("tscan_config_canfd_by_baudrate"));
    a.TxCanAsync = reinterpret_cast<decltype(a.TxCanAsync)>(
        lib->resolve("tscan_transmit_can_async"));
    a.TxCanFdAsync = reinterpret_cast<decltype(a.TxCanFdAsync)>(
        lib->resolve("tscan_transmit_canfd_async"));
    a.RxCan = reinterpret_cast<decltype(a.RxCan)>(lib->resolve("tsfifo_receive_can_msgs"));
    a.RxCanFd = reinterpret_cast<decltype(a.RxCanFd)>(lib->resolve("tsfifo_receive_canfd_msgs"));
    a.ClearCan = reinterpret_cast<decltype(a.ClearCan)>(
        lib->resolve("tsfifo_clear_can_receive_buffers"));
    a.ClearCanFd = reinterpret_cast<decltype(a.ClearCanFd)>(
        lib->resolve("tsfifo_clear_canfd_receive_buffers"));
    a.CanFrameCount = reinterpret_cast<decltype(a.CanFrameCount)>(
        lib->resolve("tsfifo_read_can_buffer_frame_count"));
    a.ErrorText = reinterpret_cast<decltype(a.ErrorText)>(
        lib->resolve("tscan_get_error_description"));

    a.ok = a.Initialize && a.ScanDevices && a.Connect && a.Disconnect
        && a.ConfigCan && a.TxCanAsync && a.RxCan;
    if (!a.ok) {
        OPENBUS_LOG_ERROR("TongXing", "libTSCAN.dll missing required exports");
        lib->unload();
        delete lib;
        return a;
    }

    a.Initialize(1, 0, 1); // FIFO on, error frames off, hardware time on
    return a;
#endif
}

QString CanDeviceTongXing::statusText(unsigned code)
{
    const Api &a = api();
    if (a.ErrorText) {
        const char *desc = nullptr;
        if (a.ErrorText(code, &desc) == kOk && desc && desc[0])
            return QString::fromLocal8Bit(desc);
    }
    return QStringLiteral("status=%1").arg(code);
}

CanDeviceTongXing::CanDeviceTongXing(int deviceIndex)
    : m_preferredIndex(deviceIndex)
{
}

CanDeviceTongXing::~CanDeviceTongXing()
{
    close();
}

bool CanDeviceTongXing::isAvailable()
{
    return api().ok;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceTongXing::enumerate()
{
    std::vector<DeviceInfo> out;
    const Api &a = api();
    if (!a.ok)
        return out;

    unsigned count = 0;
    if (a.ScanDevices(&count) != kOk || count == 0)
        return out;

    for (unsigned i = 0; i < count; ++i) {
        const char *mfr = nullptr;
        const char *product = nullptr;
        const char *serial = nullptr;
        if (a.GetDeviceInfo)
            a.GetDeviceInfo(i, &mfr, &product, &serial);

        DeviceInfo di;
        di.brand = Brand::TongXing;
        di.deviceType = int(i);
        di.deviceIndex = int(i);
        di.channels = 1;
        di.driverId = QStringLiteral("tongxing");
        di.hasHwTimestamp = true;

        QString name = fromCStr(product);
        if (name.isEmpty())
            name = QStringLiteral("TOSUN #%1").arg(i);
        const QString sn = fromCStr(serial);
        if (!sn.isEmpty())
            name += QStringLiteral(" SN%1").arg(sn);

        // Brief connect to learn channel count and FD capability.
        if (a.Connect && a.GetCanChannelCount && a.Disconnect) {
            Handle h = 0;
            QByteArray serialBytes = sn.toLocal8Bit();
            const char *serialArg = sn.isEmpty() ? nullptr : serialBytes.constData();
            if (a.Connect(serialArg, &h) == kOk && h != 0) {
                int chn = 1;
                unsigned char fd = 0;
                if (a.GetCanChannelCount(h, &chn, &fd) == kOk && chn > 0)
                    di.channels = chn;
                if (fd)
                    name += QStringLiteral(" (FD)");
                a.Disconnect(h);
            }
        }
        di.name = name;
        out.push_back(di);
    }
    return out;
}

bool CanDeviceTongXing::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const Api &a = api();
    if (!a.ok) {
        OPENBUS_LOG_ERROR("TongXing", "libTSCAN.dll not available");
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

    const char *mfr = nullptr;
    const char *product = nullptr;
    const char *serial = nullptr;
    if (a.GetDeviceInfo)
        a.GetDeviceInfo(unsigned(index), &mfr, &product, &serial);

    m_deviceName = fromCStr(product);
    if (m_deviceName.isEmpty())
        m_deviceName = QStringLiteral("TOSUN #%1").arg(index);
    m_serial = fromCStr(serial);
    if (!m_serial.isEmpty())
        m_deviceName += QStringLiteral(" SN%1").arg(m_serial);

    QByteArray serialBytes = m_serial.toLocal8Bit();
    const char *serialArg = m_serial.isEmpty() ? nullptr : serialBytes.constData();
    Handle h = 0;
    unsigned st = a.Connect(serialArg, &h);
    if (st != kOk || h == 0) {
        OPENBUS_LOG_ERROR("TongXing", "tscan_connect failed: {}",
                          statusText(st).toStdString());
        return false;
    }
    m_handle = h;

    int chnCount = 1;
    unsigned char fdFlag = 0;
    if (a.GetCanChannelCount)
        a.GetCanChannelCount(m_handle, &chnCount, &fdFlag);
    m_fdCapable = fdFlag != 0;
    if (m_channel >= chnCount) {
        OPENBUS_LOG_ERROR("TongXing", "channel {} out of range (count {})",
                          m_channel, chnCount);
        a.Disconnect(m_handle);
        m_handle = 0;
        return false;
    }

    m_canFd = canFd && m_fdCapable && a.ConfigCanFd && a.TxCanFdAsync && a.RxCanFd;
    if (canFd && !m_canFd) {
        OPENBUS_LOG_WARN("TongXing",
            "device classic-only or FD API missing; opening Classic CAN");
    }

    const double arbKbps = (arbBaud > 0 ? arbBaud : 500000) / 1000.0;
    if (m_canFd) {
        const double dataKbps = (dataBaud > 0 ? dataBaud : 2000000) / 1000.0;
        st = a.ConfigCanFd(m_handle, m_channel, arbKbps, dataKbps,
                           kCtrlIso, kCtrlNormal, 1);
        if (st != kOk) {
            OPENBUS_LOG_ERROR("TongXing", "tscan_config_canfd_by_baudrate failed: {}",
                              statusText(st).toStdString());
            a.Disconnect(m_handle);
            m_handle = 0;
            return false;
        }
        if (a.ClearCanFd)
            a.ClearCanFd(m_handle, m_channel);
    } else {
        st = a.ConfigCan(m_handle, unsigned(m_channel), arbKbps, 1);
        if (st != kOk) {
            OPENBUS_LOG_ERROR("TongXing", "tscan_config_can_by_baudrate failed: {}",
                              statusText(st).toStdString());
            a.Disconnect(m_handle);
            m_handle = 0;
            return false;
        }
        if (a.ClearCan)
            a.ClearCan(m_handle, m_channel);
    }

    m_opened = true;
    OPENBUS_LOG_INFO("TongXing", "opened {} (idx={}, ch={}, arb={}, data={}, fd={})",
                     m_deviceName.toStdString(), index, m_channel, arbBaud, dataBaud, m_canFd);
    return true;
}

void CanDeviceTongXing::close()
{
    if (!m_opened && m_handle == 0)
        return;

    const Api &a = api();
    if (m_handle != 0 && a.Disconnect)
        a.Disconnect(m_handle);
    m_handle = 0;
    m_opened = false;
    OPENBUS_LOG_INFO("TongXing", "closed {}", m_deviceName.toStdString());
}

int CanDeviceTongXing::send(const CanFrame &frame)
{
    if (!m_opened || m_handle == 0)
        return 0;

    const Api &a = api();
    if (m_canFd) {
        if (!a.TxCanFdAsync)
            return 0;
        TlibCanFd msg;
        std::memset(&msg, 0, sizeof(msg));
        msg.idxChn = static_cast<unsigned char>(m_channel);
        msg.properties = kPropTx;
        if (frame.extended)
            msg.properties = static_cast<unsigned char>(msg.properties | kPropExt);
        const int nbytes = qMin(64, frame.data.size());
        msg.dlc = CanFrame::lengthToDlc(nbytes);
        msg.fdProperties = kFdEdl;
        if (frame.bitrateSwitch)
            msg.fdProperties = static_cast<unsigned char>(msg.fdProperties | kFdBrs);
        if (frame.errorState)
            msg.fdProperties = static_cast<unsigned char>(msg.fdProperties | kFdEsi);
        msg.identifier = static_cast<std::int32_t>(frame.id & 0x1FFFFFFFu);
        if (nbytes > 0)
            std::memcpy(msg.data, frame.data.constData(), size_t(nbytes));
        return a.TxCanFdAsync(m_handle, &msg) == kOk ? 1 : 0;
    }

    if (!a.TxCanAsync)
        return 0;
    TlibCan msg;
    std::memset(&msg, 0, sizeof(msg));
    msg.idxChn = static_cast<unsigned char>(m_channel);
    msg.properties = kPropTx;
    if (frame.extended)
        msg.properties = static_cast<unsigned char>(msg.properties | kPropExt);
    const int nbytes = qMin(8, frame.data.size());
    msg.dlc = static_cast<unsigned char>(nbytes);
    msg.identifier = static_cast<std::int32_t>(frame.id & 0x1FFFFFFFu);
    if (nbytes > 0)
        std::memcpy(msg.data, frame.data.constData(), size_t(nbytes));
    return a.TxCanAsync(m_handle, &msg) == kOk ? 1 : 0;
}

int CanDeviceTongXing::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
    if (!m_opened || m_handle == 0)
        return 0;

    const Api &a = api();

#ifdef _WIN32
    if (timeoutMs > 0)
        Sleep(static_cast<DWORD>(qMin(timeoutMs, 20)));
    else if (timeoutMs < 0)
        Sleep(5);
#else
    Q_UNUSED(timeoutMs);
#endif

    if (m_canFd) {
        if (!a.RxCanFd)
            return 0;
        TlibCanFd buf[64];
        int n = 64;
        const unsigned st = a.RxCanFd(m_handle, buf, &n,
                                      static_cast<unsigned char>(m_channel), kRxTx);
        if (st != kOk || n <= 0)
            return 0;
        n = qMin(n, 64);
        outFrames.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            const TlibCanFd &msg = buf[i];
            CanFrame frame;
            frame.id = quint32(msg.identifier) & 0x1FFFFFFFu;
            frame.extended = (msg.properties & kPropExt) != 0;
            frame.fd = true;
            frame.bitrateSwitch = (msg.fdProperties & kFdBrs) != 0;
            frame.errorState = (msg.fdProperties & kFdEsi) != 0;
            if (msg.properties & kPropErr)
                frame.id |= 0x20000000u;
            const int nbytes = CanFrame::dlcToLength(msg.dlc);
            frame.dlc = msg.dlc;
            frame.data = QByteArray(reinterpret_cast<const char *>(msg.data), nbytes);
            frame.channel = static_cast<quint8>(m_channel + 1);
            frame.direction = (msg.properties & kPropTx) ? CanFrame::Tx : CanFrame::Rx;
            if (msg.timeUs > 0)
                frame.timestampNs = static_cast<quint64>(msg.timeUs) * 1000ull;
            outFrames.push_back(std::move(frame));
        }
        return static_cast<int>(outFrames.size());
    }

    if (!a.RxCan)
        return 0;
    TlibCan buf[64];
    int n = 64;
    const unsigned st = a.RxCan(m_handle, buf, &n,
                                static_cast<unsigned char>(m_channel), kRxTx);
    if (st != kOk || n <= 0)
        return 0;
    n = qMin(n, 64);
    outFrames.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const TlibCan &msg = buf[i];
        CanFrame frame;
        frame.id = quint32(msg.identifier) & 0x1FFFFFFFu;
        frame.extended = (msg.properties & kPropExt) != 0;
        if (msg.properties & kPropErr)
            frame.id |= 0x20000000u;
        const int nbytes = int(qMin(8, int(msg.dlc)));
        frame.dlc = CanFrame::lengthToDlc(nbytes);
        frame.data = QByteArray(reinterpret_cast<const char *>(msg.data), nbytes);
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = (msg.properties & kPropTx) ? CanFrame::Tx : CanFrame::Rx;
        if (msg.timeUs > 0)
            frame.timestampNs = static_cast<quint64>(msg.timeUs) * 1000ull;
        outFrames.push_back(std::move(frame));
    }
    return static_cast<int>(outFrames.size());
}

int CanDeviceTongXing::pendingCount() const
{
    if (!m_opened || m_handle == 0)
        return 0;
    const Api &a = api();
    if (!a.CanFrameCount)
        return 1;
    int count = 0;
    if (a.CanFrameCount(m_handle, m_channel, &count) != 0)
        return 0;
    return count;
}

QString CanDeviceTongXing::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("TOSUN") : m_deviceName;
}
