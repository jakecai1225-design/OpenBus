#include "candevice_candle.h"
#include "logging.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLibrary>

#include <chrono>
#include <cstring>

// ============================================================
//  GS_USB protocol constants (Linux gs_usb.c / candleLight_fw gs_usb.h)
//  Multi-byte fields are little-endian; Windows/x64 host is LE → memcpy OK
// ============================================================
namespace {

constexpr quint8 kEpIn = 0x81;
constexpr quint8 kEpOut = 0x02;

constexpr quint8 kBmReqOut = 0x41;
constexpr quint8 kBmReqIn = 0xC1;

constexpr quint8 kBreqHostFormat = 0;
constexpr quint8 kBreqBittiming = 1;
constexpr quint8 kBreqMode = 2;
constexpr quint8 kBreqBtConst = 4;
constexpr quint8 kBreqDeviceConfig = 5;
constexpr quint8 kBreqDataBittiming = 10;
constexpr quint8 kBreqBtConstExt = 11;

constexpr quint32 kByteOrderMagic = 0x0000beef;
constexpr quint32 kEchoIdRx = 0xffffffff;

constexpr quint8 kModeReset = 0;
constexpr quint8 kModeStart = 1;

constexpr quint32 kFeatHwTimestamp = (1u << 4);
constexpr quint32 kFeatFd = (1u << 8);
constexpr quint32 kModeHwTimestamp = (1u << 4);
constexpr quint32 kModeFd = (1u << 8);

constexpr quint8 kFlagOverflow = 0x01;
constexpr quint8 kFlagFd = 0x02;
constexpr quint8 kFlagBrs = 0x04;
constexpr quint8 kFlagEsi = 0x08;

constexpr quint32 kCanEffFlag = 0x80000000u;
constexpr quint32 kCanIdMask = 0x1fffffffu;

constexpr int kFrameHdrSize = 12;
constexpr int kClassicTsOff = 20;
constexpr int kClassicTxSize = 20;
constexpr int kFdTsOff = 76;
constexpr int kFdTxSize = 76;
constexpr int kRxBufSize = 80;

constexpr int kLibusbErrTimeout = -7;

/// VID/PID whitelist: kernel gs_usb_table + common CANable targets
struct VidPid {
    quint16 vid;
    quint16 pid;
    const char *model;
};
constexpr VidPid kWhitelist[] = {
    { 0x1d50, 0x606f, "GS_USB / candleLight" },
    { 0x1209, 0x8c00, "CANable (candle)" },
    { 0x1209, 0x2323, "candleLight" },
    { 0x1209, 0xca01, "CANnectivity" },
    { 0x1cd2, 0x606f, "CES CANext FD" },
    { 0x16d0, 0x10b8, "ABE CANDebugger FD" },
    { 0x16d0, 0x0f30, "Xylanta Saint3" },
};

} // namespace

// ============================================================
//  libusb dynamic load (shared singleton, never unloaded)
// ============================================================

CanDeviceCandle::LibUsb &CanDeviceCandle::usb()
{
    static LibUsb u;
    static bool tried = false;
    if (tried)
        return u;
    tried = true;

    auto resolveAll = [](QLibrary &lib) {
        u.init = reinterpret_cast<int (*)(void **)>(lib.resolve("libusb_init"));
        u.exit = reinterpret_cast<void (*)(void *)>(lib.resolve("libusb_exit"));
        u.get_device_list = reinterpret_cast<long (*)(void *, void ***)>(lib.resolve("libusb_get_device_list"));
        u.free_device_list = reinterpret_cast<void (*)(void **, int)>(lib.resolve("libusb_free_device_list"));
        u.get_device_descriptor = reinterpret_cast<int (*)(void *, RawDeviceDescriptor *)>(lib.resolve("libusb_get_device_descriptor"));
        u.bus_number = reinterpret_cast<quint8 (*)(void *)>(lib.resolve("libusb_get_bus_number"));
        u.dev_address = reinterpret_cast<quint8 (*)(void *)>(lib.resolve("libusb_get_device_address"));
        u.open = reinterpret_cast<int (*)(void *, void **)>(lib.resolve("libusb_open"));
        u.close = reinterpret_cast<void (*)(void *)>(lib.resolve("libusb_close"));
        u.ref_device = reinterpret_cast<void *(*)(void *)>(lib.resolve("libusb_ref_device"));
        u.unref_device = reinterpret_cast<void (*)(void *)>(lib.resolve("libusb_unref_device"));
        u.claim_interface = reinterpret_cast<int (*)(void *, int)>(lib.resolve("libusb_claim_interface"));
        u.release_interface = reinterpret_cast<int (*)(void *, int)>(lib.resolve("libusb_release_interface"));
        u.auto_detach = reinterpret_cast<int (*)(void *, int)>(lib.resolve("libusb_set_auto_detach_kernel_driver"));
        u.control = reinterpret_cast<int (*)(void *, quint8, quint8, quint16, quint16, quint8 *, quint16, unsigned)>(lib.resolve("libusb_control_transfer"));
        u.bulk = reinterpret_cast<int (*)(void *, quint8, quint8 *, int, int *, unsigned)>(lib.resolve("libusb_bulk_transfer"));
        u.ok = u.init && u.exit && u.get_device_list && u.free_device_list
            && u.get_device_descriptor && u.bus_number && u.dev_address
            && u.open && u.close && u.ref_device && u.unref_device
            && u.claim_interface && u.release_interface
            && u.control && u.bulk;
        return u.ok;
    };

    // Load order (plan §7.3): drivers/candle/vendor → app dir → PATH
    const QString exeDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        exeDir + QStringLiteral("/drivers/candle/vendor/libusb-1.0.dll"),
        exeDir + QStringLiteral("/libusb-1.0.dll"),
    };
    QLibrary *loaded = nullptr;
    for (const QString &c : candidates) {
        if (!QFile::exists(c))
            continue;
        auto *lib = new QLibrary(c);
        if (lib->load() && resolveAll(*lib)) {
            loaded = lib;
            break;
        }
        OPENBUS_LOG_WARN("Candle", "load {} failed: {}", c.toStdString(),
                         lib->errorString().toStdString());
        delete lib;
    }
    if (!loaded) {
        auto *lib = new QLibrary(QStringLiteral("libusb-1.0"));
        if (lib->load() && resolveAll(*lib))
            loaded = lib;
        else
            delete lib;
    }
    if (!loaded)
        return u;

    // Explicit default-context init (some Windows builds crash on NULL lazy init)
    if (u.init(nullptr) != 0) {
        OPENBUS_LOG_WARN("Candle", "libusb_init failed");
        u.ok = false;
        return u;
    }
    return u;
}

bool CanDeviceCandle::isAvailable()
{
    return usb().ok;
}

CanDeviceCandle::CanDeviceCandle(int subType)
{
    (void)subType;
}

CanDeviceCandle::~CanDeviceCandle()
{
    close();
}

QString CanDeviceCandle::modelForVidPid(quint16 vid, quint16 pid)
{
    for (const auto &w : kWhitelist)
        if (w.vid == vid && w.pid == pid)
            return QString::fromLatin1(w.model);
    return QString();
}

void CanDeviceCandle::releaseMatches(std::vector<UsbMatch> &matches)
{
    const LibUsb &u = usb();
    for (UsbMatch &m : matches) {
        if (m.dev && u.unref_device)
            u.unref_device(m.dev);
        m.dev = nullptr;
    }
    matches.clear();
}

std::vector<CanDeviceCandle::UsbMatch> CanDeviceCandle::collectWhitelisted()
{
    std::vector<UsbMatch> out;
    const LibUsb &u = usb();
    if (!u.ok)
        return out;

    void **list = nullptr;
    const long n = u.get_device_list(nullptr, &list);
    if (n < 0 || !list)
        return out;
    for (long i = 0; i < n; ++i) {
        UsbMatch m;
        m.dev = list[i];
        if (u.get_device_descriptor(m.dev, &m.desc) != 0)
            continue;
        m.model = modelForVidPid(m.desc.idVendor, m.desc.idProduct);
        if (m.model.isEmpty())
            continue;
        m.bus = u.bus_number(m.dev);
        m.addr = u.dev_address(m.dev);
        // Keep device alive after free_device_list(..., 1)
        u.ref_device(m.dev);
        out.push_back(m);
    }
    u.free_device_list(list, 1);
    return out;
}

bool CanDeviceCandle::probeDeviceConfig(const UsbMatch &m, quint8 *channelsOut)
{
    const LibUsb &u = usb();
    void *h = nullptr;
    if (u.open(m.dev, &h) != 0)
        return false;
    quint8 conf[12] = {};
    quint8 channels = 0;
    if (devCtrlIn(h, kBreqDeviceConfig, 0, conf, sizeof(conf)))
        channels = conf[3];
    u.close(h);
    if (channels < 1 || channels > 8)
        return false;
    if (channelsOut)
        *channelsOut = channels;
    return true;
}

std::vector<CanDeviceCandle::UsbMatch> CanDeviceCandle::collectOpenable()
{
    std::vector<UsbMatch> out;
    auto matches = collectWhitelisted();
    for (UsbMatch &m : matches) {
        quint8 channels = 0;
        if (!probeDeviceConfig(m, &channels)) {
            // Copy packed USB descriptor fields before fmt (cannot bind packed refs).
            const auto vid = static_cast<unsigned>(m.desc.idVendor);
            const auto pid = static_cast<unsigned>(m.desc.idProduct);
            OPENBUS_LOG_WARN("Candle",
                "found {} ({:04x}:{:04x}) but cannot open — bind WinUSB/libusb?",
                m.model.toStdString(), vid, pid);
            continue;
        }
        m.channels = channels;
        out.push_back(m);
        m.dev = nullptr; // ownership moved
    }
    releaseMatches(matches);
    return out;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceCandle::enumerate()
{
    std::vector<DeviceInfo> out;
    if (!usb().ok)
        return out;

    auto matches = collectOpenable();
    for (const UsbMatch &m : matches) {
        DeviceInfo di;
        di.brand = Brand::Candle;
        di.name = QStringLiteral("%1 @ %2-%3").arg(m.model).arg(int(m.bus)).arg(int(m.addr));
        di.deviceType = 0;
        di.deviceIndex = int(out.size());
        di.channels = m.channels;
        di.driverId = QStringLiteral("candle");
        out.push_back(di);
    }
    releaseMatches(matches);
    return out;
}

// ============================================================
//  控制传输辅助
// ============================================================

bool CanDeviceCandle::devCtrlOut(void *handle, quint8 breq, quint16 wValue, const void *data, quint16 len)
{
    return usb().control(handle, kBmReqOut, breq, wValue, 0,
                         const_cast<quint8 *>(static_cast<const quint8 *>(data)), len, 1000)
        == int(len);
}

bool CanDeviceCandle::devCtrlIn(void *handle, quint8 breq, quint16 wValue, void *data, quint16 len)
{
    return usb().control(handle, kBmReqIn, breq, wValue, 0,
                         static_cast<quint8 *>(data), len, 1000)
        == int(len);
}

bool CanDeviceCandle::ctrlOut(quint8 breq, quint16 wValue, const void *data, quint16 len)
{
    return m_handle && devCtrlOut(m_handle, breq, wValue, data, len);
}

bool CanDeviceCandle::ctrlIn(quint8 breq, quint16 wValue, void *data, quint16 len)
{
    return m_handle && devCtrlIn(m_handle, breq, wValue, data, len);
}

// ============================================================
//  打开 / 关闭
// ============================================================

bool CanDeviceCandle::readBtConst(int channel, bool extended, BtConstRaw *arb, BtConstRaw *data)
{
    // BT_CONST：{feature, fclk, tseg1_min/max, tseg2_min/max, sjw_max, brp_min/max, brp_inc} = 40 字节
    quint8 buf[72] = {};
    if (!ctrlIn(kBreqBtConst, quint16(channel), buf, 40))
        return false;
    std::memcpy(arb, buf, sizeof(BtConstRaw));

    if (!extended)
        return true;
    // BT_CONST_EXT：前 40 字节同 BT_CONST，追加 32 字节数据段 tseg·brp 范围（8×u32，无 feature/fclk）
    if (ctrlIn(kBreqBtConstExt, quint16(channel), buf, 72)) {
        *data = *arb;   // feature/fclk 沿用仲裁段
        std::memcpy(reinterpret_cast<quint8 *>(data) + 8, buf + 40, 32);
    } else {
        *data = *arb; // 固件未实现扩展查询时退化为仲裁段参数
    }
    return true;
}

bool CanDeviceCandle::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
    if (m_opened)
        close();

    const LibUsb &u = usb();
    if (!u.ok) {
        OPENBUS_LOG_ERROR("Candle",
            "libusb-1.0.dll missing (place under drivers/candle/vendor or app dir)");
        return false;
    }
    if (channel < 0)
        channel = 0;

    // Same index space as enumerate() (openable DEVICE_CONFIG devices only)
    auto matches = collectOpenable();
    if (devIndex < 0 || devIndex >= int(matches.size())) {
        OPENBUS_LOG_ERROR("Candle", "device index {} out of range ({} openable)",
                          devIndex, int(matches.size()));
        releaseMatches(matches);
        return false;
    }
    const UsbMatch chosen = matches[size_t(devIndex)];
    matches[size_t(devIndex)].dev = nullptr; // ownership moved to chosen
    releaseMatches(matches);

    void *h = nullptr;
    if (u.open(chosen.dev, &h) != 0) {
        OPENBUS_LOG_ERROR("Candle", "open {} failed (busy or bad driver binding)",
                          chosen.model.toStdString());
        u.unref_device(chosen.dev);
        return false;
    }
    u.unref_device(chosen.dev); // libusb_open holds its own ref
    m_handle = h;
    m_channel = channel;
    if (u.auto_detach)
        u.auto_detach(m_handle, 1);
    if (u.claim_interface(m_handle, 0) != 0) {
        OPENBUS_LOG_ERROR("Candle",
            "claim interface 0 failed (busy or not bound to WinUSB/libusb)");
        close();
        return false;
    }

    const quint32 magic = kByteOrderMagic;
    if (!ctrlOut(kBreqHostFormat, 0, &magic, 4)) {
        OPENBUS_LOG_ERROR("Candle", "HOST_FORMAT handshake failed");
        close();
        return false;
    }

    quint8 conf[12] = {};
    quint8 channels = 0;
    if (ctrlIn(kBreqDeviceConfig, 0, conf, sizeof(conf)))
        channels = conf[3];
    if (channels < 1 || channel >= channels) {
        OPENBUS_LOG_ERROR("Candle", "channel {} invalid (device has {} channel(s))",
                          channel, int(channels));
        close();
        return false;
    }

    BtConstRaw arbConst{}, dataConst{};
    if (!readBtConst(channel, canFd, &arbConst, &dataConst)) {
        OPENBUS_LOG_ERROR("Candle", "BT_CONST read failed (channel {})", channel);
        close();
        return false;
    }
    m_hwTs = (arbConst.feature & kFeatHwTimestamp) != 0;
    const bool fdSupported = (arbConst.feature & kFeatFd) != 0;
    if (canFd && !fdSupported) {
        OPENBUS_LOG_ERROR("Candle", "{} does not support CAN FD",
                          chosen.model.toStdString());
        close();
        return false;
    }

    BtRaw timing{};
    if (!calcBittiming(arbConst, arbBaud, &timing)) {
        OPENBUS_LOG_ERROR("Candle",
            "arbitration bitrate {} cannot divide device clock {} Hz exactly",
            arbBaud, arbConst.fclk);
        close();
        return false;
    }
    if (!ctrlOut(kBreqBittiming, quint16(channel), &timing, sizeof(BtRaw))) {
        OPENBUS_LOG_ERROR("Candle", "set arbitration bittiming failed");
        close();
        return false;
    }
    if (canFd) {
        const int effDataBaud = dataBaud > 0 ? dataBaud : 2000000;
        BtRaw dataTiming{};
        if (!calcBittiming(dataConst, effDataBaud, &dataTiming)) {
            OPENBUS_LOG_ERROR("Candle", "data bitrate {} cannot divide exactly", effDataBaud);
            close();
            return false;
        }
        if (!ctrlOut(kBreqDataBittiming, quint16(channel), &dataTiming, sizeof(BtRaw))) {
            OPENBUS_LOG_ERROR("Candle", "set data bittiming failed");
            close();
            return false;
        }
    }

    quint8 mode[8] = {};
    quint32 le32 = kModeReset;
    std::memcpy(mode, &le32, 4);
    if (!ctrlOut(kBreqMode, quint16(channel), mode, sizeof(mode))) {
        OPENBUS_LOG_ERROR("Candle", "MODE RESET failed");
        close();
        return false;
    }
    quint32 feature = 0;
    if (m_hwTs)
        feature |= kModeHwTimestamp;
    if (canFd)
        feature |= kModeFd;
    le32 = kModeStart;
    std::memcpy(mode, &le32, 4);
    std::memcpy(mode + 4, &feature, 4);
    if (!ctrlOut(kBreqMode, quint16(channel), mode, sizeof(mode))) {
        OPENBUS_LOG_ERROR("Candle", "MODE START failed");
        close();
        return false;
    }

    m_canFd = canFd;
    m_opened = true;
    m_tsSynced = false;
    m_echoCounter = 0;
    m_deviceName = QStringLiteral("%1 #%2 ch%3").arg(chosen.model).arg(devIndex).arg(channel + 1);
    OPENBUS_LOG_INFO("Candle", "opened {} (arb {}{} , {})",
                     m_deviceName.toStdString(), arbBaud,
                     canFd ? QStringLiteral(", data %1").arg(dataBaud).toStdString() : std::string(),
                     m_hwTs ? "hw timestamp" : "sw timestamp");
    return true;
}

void CanDeviceCandle::close()
{
    if (m_handle) {
        quint8 mode[8] = {}; // mode = RESET, feature = 0
        ctrlOut(kBreqMode, quint16(m_channel), mode, sizeof(mode));
        usb().release_interface(m_handle, 0);
        usb().close(m_handle);
        m_handle = nullptr;
    }
    m_opened = false;
    m_tsSynced = false;
}

// ============================================================
//  位时序计算（87.5% 采样点；仅接受精确分频，宁可失败不可偏差）
// ============================================================

bool CanDeviceCandle::calcBittiming(const BtConstRaw &c, int bitrate, BtRaw *out)
{
    if (bitrate <= 0 || c.fclk == 0 || c.brpInc == 0 || !c.brpMax)
        return false;
    const double targetSp = 0.875;
    bool found = false;
    double bestErr = 1e9;

    for (quint64 brp = c.brpMin; brp <= c.brpMax; brp += c.brpInc) {
        const quint64 denom = brp * quint64(bitrate);
        if (c.fclk % denom != 0)
            continue; // 无法精确整除 → 实际波特率将偏离目标
        const quint32 tq = quint32(c.fclk / denom);
        const quint32 minTq = c.tseg1Min + c.tseg2Min + 1;
        const quint32 maxTq = c.tseg1Max + c.tseg2Max + 1;
        if (tq < minTq || tq > maxTq)
            continue;

        quint32 tseg1 = quint32(targetSp * tq - 1.0 + 0.5);
        quint32 tseg2 = tq - tseg1 - 1;
        if (tseg1 < c.tseg1Min) { tseg1 = c.tseg1Min; tseg2 = tq - tseg1 - 1; }
        if (tseg1 > c.tseg1Max) { tseg1 = c.tseg1Max; tseg2 = tq - tseg1 - 1; }
        if (tseg2 < c.tseg2Min) { tseg2 = c.tseg2Min; tseg1 = tq - tseg2 - 1; }
        if (tseg2 > c.tseg2Max) { tseg2 = c.tseg2Max; tseg1 = tq - tseg2 - 1; }
        if (tseg1 < c.tseg1Min || tseg1 > c.tseg1Max
            || tseg2 < c.tseg2Min || tseg2 > c.tseg2Max)
            continue;

        const double sp = double(tseg1 + 1) / double(tq);
        const double err = sp > targetSp ? sp - targetSp : targetSp - sp;
        if (!found || err < bestErr) {
            found = true;
            bestErr = err;
            out->propSeg = 0; // 固件按 prop_seg + phase_seg1 之和校验 tseg1
            out->phaseSeg1 = tseg1;
            out->phaseSeg2 = tseg2;
            out->sjw = qMin(c.sjwMax, tseg2);
            out->brp = quint32(brp);
        }
    }
    return found;
}

// ============================================================
//  收发
// ============================================================

int CanDeviceCandle::send(const CanFrame &frame)
{
    if (!m_opened || !m_handle)
        return 0;

    quint8 buf[kRxBufSize];
    std::memset(buf, 0, sizeof(buf));

    const bool fd = frame.fd;
    const int len = qMin(fd ? 64 : 8, int(frame.data.size()));

    quint32 echo = ++m_echoCounter;
    if (echo == kEchoIdRx)
        echo = ++m_echoCounter; // 0xffffffff 保留给接收帧
    quint32 canId = frame.id & kCanIdMask;
    if (frame.extended)
        canId |= kCanEffFlag;
    quint8 flags = 0;
    if (fd) {
        flags |= kFlagFd;
        if (frame.bitrateSwitch)
            flags |= kFlagBrs;
        if (frame.errorState)
            flags |= kFlagEsi;
    }
    std::memcpy(buf + 0, &echo, 4);
    std::memcpy(buf + 4, &canId, 4);
    buf[8] = quint8(len); // GS_USB 用字节长度，非 DLC 码
    buf[9] = quint8(m_channel);
    buf[10] = flags;
    std::memcpy(buf + kFrameHdrSize, frame.data.constData(), len);

    // 发送帧不带时间戳：经典 20 字节 / FD 76 字节（与内核驱动一致）
    int transferred = 0;
    const int size = fd ? kFdTxSize : kClassicTxSize;
    if (usb().bulk(m_handle, kEpOut, buf, size, &transferred, 100) != 0) {
        OPENBUS_LOG_WARN("Candle", "send failed (busy or unplugged)");
        return 0;
    }
    return 1;
}

int CanDeviceCandle::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    if (!m_opened || !m_handle)
        return 0;
    QElapsedTimer timer;
    timer.start();
    int got = 0;
    quint8 buf[kRxBufSize];

    forever {
        const int slice = (timeoutMs < 0)
            ? 50
            : qBound(0, timeoutMs - int(timer.elapsed()), 50);
        int transferred = 0;
        const int rc = usb().bulk(m_handle, kEpIn, buf, sizeof(buf),
                                  &transferred, unsigned(slice > 0 ? slice : 1));
        if (rc == 0 && transferred >= kFrameHdrSize) {
            // ---- 解析一帧（一次 bulk 传输 = 一帧，固件不粘包） ----
            quint32 echoId = 0, canId = 0;
            std::memcpy(&echoId, buf + 0, 4);
            std::memcpy(&canId, buf + 4, 4);
            const quint8 dlc = buf[8];
            const quint8 ch = buf[9];
            const quint8 flg = buf[10];
            if (echoId != kEchoIdRx)
                continue; // 发送回环帧：跳过（上层 send 路径已记账）
            if (flg & kFlagOverflow) {
                OPENBUS_LOG_WARN("Candle", "device buffer overflow — frames may be lost");
                continue;
            }

            const bool fd = (flg & kFlagFd) != 0;
            CanFrame frame;
            frame.extended = (canId & kCanEffFlag) != 0;
            frame.id = canId & kCanIdMask;
            frame.fd = fd;
            frame.bitrateSwitch = (flg & kFlagBrs) != 0;
            frame.errorState = (flg & kFlagEsi) != 0;
            frame.channel = quint8(ch + 1); // 1-based，与其他后端一致
            frame.direction = CanFrame::Rx;
            const int len = qMin(fd ? 64 : 8, int(dlc));
            frame.data = QByteArray(reinterpret_cast<const char *>(buf + kFrameHdrSize), len);
            frame.dlc = fd ? CanFrame::lengthToDlc(len) : quint8(len);
            if (m_hwTs) {
                const int tsOff = fd ? kFdTsOff : kClassicTsOff;
                if (transferred >= tsOff + 4) {
                    quint32 tsUs = 0;
                    std::memcpy(&tsUs, buf + tsOff, 4);
                    frame.timestampNs = hwTsToNs(tsUs);
                }
            }
            outFrames.push_back(frame);
            ++got;
            continue;
        }
        if (rc != 0 && rc != kLibusbErrTimeout) {
            OPENBUS_LOG_WARN("Candle", "recv failed (libusb err={}), stop this poll", rc);
            break;
        }
        // 超时切片：阻塞模式收齐一波即返回；限时模式等满窗口
        if (timeoutMs < 0) {
            if (got > 0)
                break;
            continue;
        }
        if (int(timer.elapsed()) >= timeoutMs)
            break;
    }
    return got;
}

int CanDeviceCandle::pendingCount() const
{
    // GS_USB 协议无接收队列深度查询，上层按 10ms 轮询 recv(10)
    return 0;
}

QString CanDeviceCandle::deviceName() const
{
    return m_deviceName.isEmpty() ? QStringLiteral("Candle USB") : m_deviceName;
}

// ============================================================
//  硬件时间戳 → steady_clock 纳秒
// ============================================================

quint64 CanDeviceCandle::hwTsToNs(quint32 rawUs)
{
    // 32 位 1MHz 计数器约 71.6 分钟回绕一次：大幅回跳视为回绕
    if (m_tsSynced && rawUs < m_lastRawTsUs && (m_lastRawTsUs - rawUs) > 0x80000000u)
        m_tsWrapUs += (Q_UINT64_C(1) << 32);
    m_lastRawTsUs = rawUs;
    const quint64 absUs = m_tsWrapUs + rawUs;

    if (!m_tsSynced) {
        // 首帧对齐：offset = 当前 steady_clock − 设备时钟
        using namespace std::chrono;
        const quint64 nowNs = quint64(duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()).count());
        m_tsOffsetNs = qint64(nowNs) - qint64(absUs * 1000);
        m_tsSynced = true;
    }
    return quint64(qint64(absUs * 1000) + m_tsOffsetNs);
}
