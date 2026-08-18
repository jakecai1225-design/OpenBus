#include "candevice_candle.h"
#include "logging.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLibrary>

#include <chrono>
#include <cstring>

// ============================================================
//  GS_USB 协议常量（参照 Linux 内核 gs_usb.c / candleLight_fw gs_usb.h）
//  多字节字段一律小端；本实现仅面向 Windows/x64（小端主机，结构体直接 memcpy）
// ============================================================
namespace {

constexpr quint8 kEpIn = 0x81;   ///< 批量输入端点
constexpr quint8 kEpOut = 0x02;  ///< 批量输出端点

// 控制请求（vendor, 接口接收方）：0x41 = OUT，0xC1 = IN；wValue=通道，wIndex=接口 0
constexpr quint8 kBmReqOut = 0x41;
constexpr quint8 kBmReqIn = 0xC1;

constexpr quint8 kBreqHostFormat = 0;
constexpr quint8 kBreqBittiming = 1;
constexpr quint8 kBreqMode = 2;
constexpr quint8 kBreqBtConst = 4;
constexpr quint8 kBreqDeviceConfig = 5;
constexpr quint8 kBreqDataBittiming = 10;
constexpr quint8 kBreqBtConstExt = 11;

constexpr quint32 kByteOrderMagic = 0x0000beef;  ///< HOST_FORMAT 小端魔数
constexpr quint32 kEchoIdRx = 0xffffffff;        ///< echo_id 为该值 = 接收帧

constexpr quint8 kModeReset = 0;
constexpr quint8 kModeStart = 1;

// 设备特征位（bt_const.feature）与请求的模式特征位（gs_device_mode.feature）
// 是两个不同位空间，数值恰有重叠（HW_TIMESTAMP/FD），不可混用
constexpr quint32 kFeatHwTimestamp = (1u << 4);
constexpr quint32 kFeatFd = (1u << 8);
constexpr quint32 kModeHwTimestamp = (1u << 4);
constexpr quint32 kModeFd = (1u << 8);

// 帧标志位（gs_host_frame.flags）
constexpr quint8 kFlagOverflow = 0x01;
constexpr quint8 kFlagFd = 0x02;
constexpr quint8 kFlagBrs = 0x04;
constexpr quint8 kFlagEsi = 0x08;

// CAN ID 标志位（与 linux/can.h 一致）
constexpr quint32 kCanEffFlag = 0x80000000u;
constexpr quint32 kCanIdMask = 0x1fffffffu;

// 帧布局：12 字节头 + data + 可选 timestamp_us
constexpr int kFrameHdrSize = 12;
constexpr int kClassicTsOff = 20;  // data[8] 之后
constexpr int kClassicTxSize = 20; // 发送不带时间戳
constexpr int kFdTsOff = 76;       // data[64] 之后
constexpr int kFdTxSize = 76;
constexpr int kRxBufSize = 80;     // fd_ts = 最大帧

constexpr int kLibusbErrTimeout = -7;

/// VID/PID 白名单：内核 gs_usb_table + 生态常用 CANable
struct VidPid {
    quint16 vid;
    quint16 pid;
    const char *model;
};
constexpr VidPid kWhitelist[] = {
    { 0x1d50, 0x606f, "GS_USB / candleLight" }, // Geschwister Schneider USB2CAN、candleLight DIY
    { 0x1209, 0x8c00, "CANable (candle)" },     // candleLight_fw 官方 CANable 目标
    { 0x1209, 0x2323, "candleLight" },          // 原版 candleLight
    { 0x1209, 0xca01, "CANnectivity" },
    { 0x1cd2, 0x606f, "CES CANext FD" },
    { 0x16d0, 0x10b8, "ABE CANDebugger FD" },
    { 0x16d0, 0x0f30, "Xylanta Saint3" },
};

} // namespace

// ============================================================
//  libusb 动态加载（共享单例，进程内永不卸载）
// ============================================================

CanDeviceCandle::LibUsb &CanDeviceCandle::usb()
{
    static LibUsb u;
    static bool tried = false;
    if (tried)
        return u;
    tried = true;

    auto resolveAll = [](QLibrary &lib) {  // 静态变量 u 无需捕获
        u.init = reinterpret_cast<int (*)(void **)>(lib.resolve("libusb_init"));
        u.exit = reinterpret_cast<void (*)(void *)>(lib.resolve("libusb_exit"));
        u.get_device_list = reinterpret_cast<long (*)(void *, void ***)>(lib.resolve("libusb_get_device_list"));
        u.free_device_list = reinterpret_cast<void (*)(void **, int)>(lib.resolve("libusb_free_device_list"));
        u.get_device_descriptor = reinterpret_cast<int (*)(void *, RawDeviceDescriptor *)>(lib.resolve("libusb_get_device_descriptor"));
        u.bus_number = reinterpret_cast<quint8 (*)(void *)>(lib.resolve("libusb_get_bus_number"));
        u.dev_address = reinterpret_cast<quint8 (*)(void *)>(lib.resolve("libusb_get_device_address"));
        u.open = reinterpret_cast<int (*)(void *, void **)>(lib.resolve("libusb_open"));
        u.close = reinterpret_cast<void (*)(void *)>(lib.resolve("libusb_close"));
        u.claim_interface = reinterpret_cast<int (*)(void *, int)>(lib.resolve("libusb_claim_interface"));
        u.release_interface = reinterpret_cast<int (*)(void *, int)>(lib.resolve("libusb_release_interface"));
        u.auto_detach = reinterpret_cast<int (*)(void *, int)>(lib.resolve("libusb_set_auto_detach_kernel_driver"));
        u.control = reinterpret_cast<int (*)(void *, quint8, quint8, quint16, quint16, quint8 *, quint16, unsigned)>(lib.resolve("libusb_control_transfer"));
        u.bulk = reinterpret_cast<int (*)(void *, quint8, quint8 *, int, int *, unsigned)>(lib.resolve("libusb_bulk_transfer"));
        u.ok = u.init && u.exit && u.get_device_list && u.free_device_list
            && u.get_device_descriptor && u.bus_number && u.dev_address
            && u.open && u.close && u.claim_interface && u.release_interface
            && u.control && u.bulk;
        return u.ok;
    };

    // 加载顺序（方案 §7.3）：drivers/candle/vendor → 应用目录 → 系统搜索路径。
    // QLibrary 故意不析构（堆分配永不 delete）：满足「加载后永不卸载」约束，
    // 避免 usb 设备状态被 DllMain 析构破坏
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
        OPENBUS_LOG_WARN("Candle", "加载 {} 失败: {}", c.toStdString(), lib->errorString().toStdString());
        delete lib; // 未成功解析的库可安全释放
    }
    if (!loaded) {
        auto *lib = new QLibrary(QStringLiteral("libusb-1.0"));
        if (lib->load() && resolveAll(*lib))
            loaded = lib;
        else
            delete lib;
    }
    if (!loaded)
        return u; // 静默失败：未装 libusb 时枚举自然为空，不刷日志

    // 显式初始化默认上下文——不依赖 libusb 的 NULL-context 惰性初始化：
    // 部分 Windows 构建（实测 PyPI wheel 1.0.29）该路径未初始化即解引用，
    // libusb_get_device_list 直接 AV 崩溃
    if (u.init(nullptr) != 0) {
        OPENBUS_LOG_WARN("Candle", "libusb_init 失败");
        u.ok = false;
        return u;
    }
    return u;
}

bool CanDeviceCandle::isAvailable()
{
    return usb().ok;
}

// ============================================================
//  构造 / 析构
// ============================================================

CanDeviceCandle::CanDeviceCandle(int subType)
{
    // 型号仅用于市场页展示：同一 GS_USB 协议，能力（FD/时间戳）由 bt_const 探测
    (void)subType;
}

CanDeviceCandle::~CanDeviceCandle()
{
    close();
}

// ============================================================
//  枚举
// ============================================================

QString CanDeviceCandle::modelForVidPid(quint16 vid, quint16 pid)
{
    for (const auto &w : kWhitelist)
        if (w.vid == vid && w.pid == pid)
            return QString::fromLatin1(w.model);
    return QString();
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
            continue; // 宁可漏不可错：严格白名单（方案 §14.5.2）
        m.bus = u.bus_number(m.dev);
        m.addr = u.dev_address(m.dev);
        out.push_back(m);
    }
    u.free_device_list(list, 1);
    return out;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceCandle::enumerate()
{
    std::vector<DeviceInfo> out;
    const LibUsb &u = usb();
    if (!u.ok)
        return out;

    for (const UsbMatch &m : collectWhitelisted()) {
        void *h = nullptr;
        if (u.open(m.dev, &h) != 0) {
            OPENBUS_LOG_WARN("Candle", "发现 {} ({:04x}:{:04x}) 但无法打开：可能未绑定 WinUSB/libusb 驱动",
                             m.model.toStdString(), m.desc.idVendor, m.desc.idProduct);
            continue;
        }
        // DEVICE_CONFIG 探测通道数（宁可漏不可错：打不开的不进列表）
        quint8 conf[12] = {};
        quint8 channels = 0;
        quint32 swVer = 0;
        if (devCtrlIn(h, kBreqDeviceConfig, 0, conf, sizeof(conf))) {
            channels = conf[3];
            std::memcpy(&swVer, conf + 4, 4);
        }
        u.close(h);
        if (channels < 1 || channels > 8) {
            OPENBUS_LOG_WARN("Candle", "{} 探测通道数异常: {}", m.model.toStdString(), int(channels));
            continue;
        }

        DeviceInfo di;
        di.brand = Brand::Candle;
        di.name = QStringLiteral("%1 @ %2-%3").arg(m.model).arg(int(m.bus)).arg(int(m.addr));
        di.deviceType = 0;
        di.deviceIndex = int(out.size());
        di.channels = channels;
        di.driverId = QStringLiteral("candle");
        out.push_back(di);
    }
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
        OPENBUS_LOG_ERROR("Candle", "libusb-1.0.dll 不可用（放置于 drivers/candle/vendor 或应用目录）");
        return false;
    }
    if (channel < 0)
        channel = 0;

    // ---- 1. 按枚举序号定位并打开设备 ----
    const auto matches = collectWhitelisted();
    if (devIndex < 0 || devIndex >= int(matches.size())) {
        OPENBUS_LOG_ERROR("Candle", "设备序号 {} 超出范围（共 {} 个）", devIndex, int(matches.size()));
        return false;
    }
    const UsbMatch &m = matches[size_t(devIndex)];
    void *h = nullptr;
    if (u.open(m.dev, &h) != 0) {
        OPENBUS_LOG_ERROR("Candle", "打开 {} 失败（设备被占用或驱动异常）", m.model.toStdString());
        return false;
    }
    m_handle = h;
    m_channel = channel;
    if (u.auto_detach)
        u.auto_detach(m_handle, 1);
    if (u.claim_interface(m_handle, 0) != 0) {
        OPENBUS_LOG_ERROR("Candle", "声明接口 0 失败（设备被占用或未绑定 WinUSB/libusb 驱动）");
        close();
        return false;
    }

    // ---- 2. HOST_FORMAT：声明小端字节序 ----
    const quint32 magic = kByteOrderMagic;
    if (!ctrlOut(kBreqHostFormat, 0, &magic, 4)) {
        OPENBUS_LOG_ERROR("Candle", "HOST_FORMAT 握手失败");
        close();
        return false;
    }

    // ---- 3. DEVICE_CONFIG：校验通道号 ----
    quint8 conf[12] = {};
    quint8 channels = 0;
    if (ctrlIn(kBreqDeviceConfig, 0, conf, sizeof(conf)))
        channels = conf[3];
    if (channels < 1 || channel >= channels) {
        OPENBUS_LOG_ERROR("Candle", "通道号 {} 非法（设备共 {} 通道）", channel, int(channels));
        close();
        return false;
    }

    // ---- 4. BT_CONST：能力探测 + 位时序计算 ----
    BtConstRaw arbConst{}, dataConst{};
    if (!readBtConst(channel, canFd, &arbConst, &dataConst)) {
        OPENBUS_LOG_ERROR("Candle", "读取 BT_CONST 失败（通道 {}）", channel);
        close();
        return false;
    }
    m_hwTs = (arbConst.feature & kFeatHwTimestamp) != 0;
    const bool fdSupported = (arbConst.feature & kFeatFd) != 0;
    if (canFd && !fdSupported) {
        OPENBUS_LOG_ERROR("Candle", "{} 不支持 CAN FD（固件特征位未置位）", m.model.toStdString());
        close();
        return false;
    }

    BtRaw timing{};
    if (!calcBittiming(arbConst, arbBaud, &timing)) {
        OPENBUS_LOG_ERROR("Candle", "仲裁段波特率 {} 无法由设备时钟 {} Hz 精确分频",
                          arbBaud, arbConst.fclk);
        close();
        return false;
    }
    if (!ctrlOut(kBreqBittiming, quint16(channel), &timing, sizeof(BtRaw))) {
        OPENBUS_LOG_ERROR("Candle", "设置仲裁段位时序失败");
        close();
        return false;
    }
    if (canFd) {
        const int effDataBaud = dataBaud > 0 ? dataBaud : 2000000;
        BtRaw dataTiming{};
        if (!calcBittiming(dataConst, effDataBaud, &dataTiming)) {
            OPENBUS_LOG_ERROR("Candle", "数据段波特率 {} 无法精确分频", effDataBaud);
            close();
            return false;
        }
        if (!ctrlOut(kBreqDataBittiming, quint16(channel), &dataTiming, sizeof(BtRaw))) {
            OPENBUS_LOG_ERROR("Candle", "设置数据段位时序失败");
            close();
            return false;
        }
    }

    // ---- 5. MODE：先复位再启动（模式特征 = 硬件时间戳 + FD） ----
    quint8 mode[8] = {};
    quint32 le32 = kModeReset;
    std::memcpy(mode, &le32, 4); // feature = 0
    if (!ctrlOut(kBreqMode, quint16(channel), mode, sizeof(mode))) {
        OPENBUS_LOG_ERROR("Candle", "MODE RESET 失败");
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
        OPENBUS_LOG_ERROR("Candle", "MODE START 失败");
        close();
        return false;
    }

    m_canFd = canFd;
    m_opened = true;
    m_tsSynced = false;
    m_echoCounter = 0;
    m_deviceName = QStringLiteral("%1 #%2 ch%3").arg(m.model).arg(devIndex).arg(channel + 1);
    OPENBUS_LOG_INFO("Candle", "已打开 {}（仲裁 {}{}，{}）",
                     m_deviceName.toStdString(), arbBaud,
                     canFd ? QStringLiteral(", 数据 %1").arg(dataBaud).toStdString() : std::string(),
                     m_hwTs ? "硬件时间戳" : "软件时间戳");
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
        OPENBUS_LOG_WARN("Candle", "发送失败（设备忙或已拔出）");
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
                OPENBUS_LOG_WARN("Candle", "设备缓冲溢出，可能有帧丢失");
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
            OPENBUS_LOG_WARN("Candle", "接收失败 (libusb err={})，停止本轮收包", rc);
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
