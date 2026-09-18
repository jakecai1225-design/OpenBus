#include "candevice_busmust.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <cstring>

// Types only — resolve exports via QLibrary (no link against BMAPI64.lib).
#include "bm_usb_def.h"

namespace {

using BM_ChannelHandle = void *;
using BM_NotificationHandle = void *;

using fn_Init = BM_StatusTypeDef (*)(void);
using fn_UnInit = BM_StatusTypeDef (*)(void);
using fn_Enumerate = BM_StatusTypeDef (*)(BM_ChannelInfoTypeDef *, int *);
using fn_OpenEx = BM_StatusTypeDef (*)(BM_ChannelHandle *, BM_ChannelInfoTypeDef *,
                                       uint32_t, BM_TerminalResistorTypeDef,
                                       const BM_BitrateTypeDef *,
                                       const BM_RxFilterTypeDef *, int);
using fn_Close = BM_StatusTypeDef (*)(BM_ChannelHandle);
using fn_ReadCanMessage = BM_StatusTypeDef (*)(BM_ChannelHandle, BM_CanMessageTypeDef *,
                                               uint32_t *, uint32_t *);
using fn_WriteCanMessage = BM_StatusTypeDef (*)(BM_ChannelHandle, BM_CanMessageTypeDef *,
                                                uint32_t, int, uint32_t *);
using fn_GetNotification = BM_StatusTypeDef (*)(BM_ChannelHandle, BM_NotificationHandle *);
using fn_WaitForNotifications = int (*)(BM_NotificationHandle *, int, int);

struct Api {
    bool ok = false;
    bool inited = false;
    QLibrary dll;
    fn_Init Init = nullptr;
    fn_UnInit UnInit = nullptr;
    fn_Enumerate Enumerate = nullptr;
    fn_OpenEx OpenEx = nullptr;
    fn_Close Close = nullptr;
    fn_ReadCanMessage ReadCanMessage = nullptr;
    fn_WriteCanMessage WriteCanMessage = nullptr;
    fn_GetNotification GetNotification = nullptr;
    fn_WaitForNotifications WaitForNotifications = nullptr;
};

QMutex &apiMutex()
{
    static QMutex m;
    return m;
}

Api &api()
{
    static Api a;
    return a;
}

QString findBmapiDll()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/drivers/busmust/vendor/BMAPI64.dll"),
        appDir + QStringLiteral("/BMAPI64.dll"),
        appDir + QStringLiteral("/driver/BMAPI64.dll"),
    };
    for (const QString &p : candidates) {
        if (QFile::exists(p))
            return QDir::toNativeSeparators(p);
    }
    return QStringLiteral("BMAPI64.dll");
}

bool loadApi()
{
    QMutexLocker lock(&apiMutex());
    Api &a = api();
    if (a.ok)
        return true;

    a.dll.setFileName(findBmapiDll());
    if (!a.dll.load()) {
        OPENBUS_LOG_ERROR("CanDeviceBusmust", "Failed to load BMAPI64.dll: {}",
                          a.dll.errorString().toStdString());
        return false;
    }

    a.Init = reinterpret_cast<fn_Init>(a.dll.resolve("BM_Init"));
    a.UnInit = reinterpret_cast<fn_UnInit>(a.dll.resolve("BM_UnInit"));
    a.Enumerate = reinterpret_cast<fn_Enumerate>(a.dll.resolve("BM_Enumerate"));
    a.OpenEx = reinterpret_cast<fn_OpenEx>(a.dll.resolve("BM_OpenEx"));
    a.Close = reinterpret_cast<fn_Close>(a.dll.resolve("BM_Close"));
    a.ReadCanMessage =
        reinterpret_cast<fn_ReadCanMessage>(a.dll.resolve("BM_ReadCanMessage"));
    a.WriteCanMessage =
        reinterpret_cast<fn_WriteCanMessage>(a.dll.resolve("BM_WriteCanMessage"));
    a.GetNotification =
        reinterpret_cast<fn_GetNotification>(a.dll.resolve("BM_GetNotification"));
    a.WaitForNotifications = reinterpret_cast<fn_WaitForNotifications>(
        a.dll.resolve("BM_WaitForNotifications"));

    if (!a.Init || !a.Enumerate || !a.OpenEx || !a.Close || !a.ReadCanMessage
        || !a.WriteCanMessage) {
        OPENBUS_LOG_ERROR("CanDeviceBusmust", "BMAPI64.dll missing required exports");
        a.dll.unload();
        return false;
    }

    // BM_Init is process-global. Builtin openbus_data and driver_busmust.dll
    // each have a separate static Api; a second Init returns ILLOPERATION.
    const BM_StatusTypeDef st = a.Init();
    if (st != BM_ERROR_OK && st != BM_ERROR_ILLOPERATION) {
        OPENBUS_LOG_ERROR("CanDeviceBusmust", "BM_Init failed: 0x{:x}",
                          static_cast<unsigned>(st));
        a.dll.unload();
        return false;
    }
    a.inited = (st == BM_ERROR_OK);
    a.ok = true;
    OPENBUS_LOG_INFO("CanDeviceBusmust", "BMAPI64.dll ready ({}, init=0x{:x})",
                     a.dll.fileName().toStdString(),
                     static_cast<unsigned>(st));
    return true;
}

int dlcToLen(unsigned dlc, bool fd)
{
    static const int kFd[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    if (!fd)
        return static_cast<int>(qMin(dlc, 8u));
    if (dlc < 16)
        return kFd[dlc];
    return 64;
}

unsigned lenToDlc(int len, bool fd)
{
    if (!fd)
        return static_cast<unsigned>(qBound(0, len, 8));
    static const int kFd[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (unsigned i = 0; i < 16; ++i) {
        if (kFd[i] >= len)
            return i;
    }
    return 15;
}

void fillBitrate(BM_BitrateTypeDef *br, int arbBaud, int dataBaud, bool canFd)
{
    std::memset(br, 0, sizeof(*br));
    br->nbitrate = static_cast<uint16_t>(qMax(1, arbBaud / 1000)); // kbps
    br->dbitrate = static_cast<uint16_t>(
        canFd ? qMax(1, dataBaud / 1000) : br->nbitrate);
    br->nsamplepos = 75;
    br->dsamplepos = 75;
}

uint32_t msgIdValue(const BM_CanMessageTypeDef &msg)
{
    if (msg.ctrl.rx.IDE)
        return BM_GET_EXT_MSG_ID(msg.id);
    return BM_GET_STD_MSG_ID(msg.id);
}

} // namespace

CanDeviceBusmust::CanDeviceBusmust(int channelIndex)
    : m_channelIndex(channelIndex)
{
}

CanDeviceBusmust::~CanDeviceBusmust()
{
    close();
}

bool CanDeviceBusmust::isAvailable()
{
    // Probe DLL only — do not BM_Init here. Init is process-global; calling it
    // from both the builtin (openbus_data) and the external driver_busmust
    // plugin poisons the other module's first real loadApi().
    const QString path = findBmapiDll();
    if (QFile::exists(path))
        return true;
    QLibrary probe(QStringLiteral("BMAPI64.dll"));
    if (probe.load()) {
        probe.unload();
        return true;
    }
    return false;
}

std::vector<ICanDevice::DeviceInfo> CanDeviceBusmust::enumerate()
{
    std::vector<DeviceInfo> list;
    if (!loadApi())
        return list;

    BM_ChannelInfoTypeDef infos[64];
    int n = 64;
    Api &a = api();
    const BM_StatusTypeDef st = a.Enumerate(infos, &n);
    if (st != BM_ERROR_OK || n <= 0) {
        OPENBUS_LOG_INFO("CanDeviceBusmust", "enumerate: none (status=0x{:x})",
                         static_cast<unsigned>(st));
        return list;
    }

    for (int i = 0; i < n && i < 64; ++i) {
        // Skip BMAPI virtual/simulation channels (BM_VIRTUAL_CAP)
        if (infos[i].cap & BM_VIRTUAL_CAP)
            continue;

        DeviceInfo di;
        di.brand = Brand::Busmust;
        di.deviceIndex = i;
        di.deviceType = infos[i].port;
        di.channels = 1;
        di.hasHwTimestamp = true;
        const QString name = QString::fromUtf8(infos[i].name).trimmed();
        di.name = name.isEmpty()
                      ? QStringLiteral("BUSMUST CAN #%1").arg(i)
                      : name;
        if (infos[i].vid || infos[i].pid) {
            di.name += QStringLiteral(" [%1:%2]")
                           .arg(infos[i].vid, 4, 16, QLatin1Char('0'))
                           .arg(infos[i].pid, 4, 16, QLatin1Char('0'))
                           .toUpper();
        }
        list.push_back(std::move(di));
    }
    OPENBUS_LOG_INFO("CanDeviceBusmust", "enumerate: found {} channel(s)",
                     static_cast<int>(list.size()));
    return list;
}

bool CanDeviceBusmust::open(int devIndex, int /*channel*/, int arbBaud, int dataBaud,
                            bool canFd)
{
    if (m_opened)
        return true;
    if (!loadApi())
        return false;

    BM_ChannelInfoTypeDef infos[64];
    int n = 64;
    Api &a = api();
    if (a.Enumerate(infos, &n) != BM_ERROR_OK || n <= 0) {
        OPENBUS_LOG_ERROR("CanDeviceBusmust", "open: enumerate failed");
        return false;
    }

    const int idx = (devIndex >= 0) ? devIndex : m_channelIndex;
    if (idx < 0 || idx >= n) {
        OPENBUS_LOG_ERROR("CanDeviceBusmust", "open: index {} out of range (n={})",
                          idx, n);
        return false;
    }

    BM_BitrateTypeDef br;
    fillBitrate(&br, arbBaud, dataBaud > 0 ? dataBaud : arbBaud, canFd);
    const uint32_t mode =
        canFd ? static_cast<uint32_t>(BM_CAN_NORMAL_MODE)
              : static_cast<uint32_t>(BM_CAN_CLASSIC_MODE);

    BM_ChannelHandle handle = nullptr;
    const BM_StatusTypeDef st = a.OpenEx(
        &handle, &infos[idx], mode, BM_TRESISTOR_120, &br, nullptr, 0);
    if (st != BM_ERROR_OK || !handle) {
        OPENBUS_LOG_ERROR("CanDeviceBusmust", "BM_OpenEx failed: 0x{:x}",
                          static_cast<unsigned>(st));
        return false;
    }

    m_handle = handle;
    m_channelIndex = idx;
    m_canFd = canFd;
    m_name = QString::fromUtf8(infos[idx].name).trimmed();
    if (m_name.isEmpty())
        m_name = QStringLiteral("BUSMUST CAN #%1").arg(idx);
    m_opened = true;
    OPENBUS_LOG_INFO("CanDeviceBusmust", "opened '{}' arb={} data={} fd={}",
                     m_name.toStdString(), arbBaud, dataBaud, canFd);
    return true;
}

void CanDeviceBusmust::close()
{
    if (!m_opened)
        return;
    Api &a = api();
    if (a.ok && a.Close && m_handle)
        a.Close(static_cast<BM_ChannelHandle>(m_handle));
    m_handle = nullptr;
    m_opened = false;
}

int CanDeviceBusmust::send(const CanFrame &frame)
{
    if (!m_opened || !m_handle)
        return -1;
    Api &a = api();

    BM_CanMessageTypeDef msg;
    std::memset(&msg, 0, sizeof(msg));
    const bool fd = m_canFd && frame.fd;
    const int len = qMin(static_cast<int>(frame.data.size()), fd ? 64 : 8);
    msg.ctrl.tx.DLC = lenToDlc(len, fd);
    msg.ctrl.tx.IDE = frame.extended ? 1 : 0;
    msg.ctrl.tx.FDF = fd ? 1 : 0;
    msg.ctrl.tx.BRS = fd ? 1 : 0;
    msg.ctrl.tx.RTR = 0;
    // Macros expand to compound statements — must not sit bare before else
    if (frame.extended) {
        BM_SET_EXT_MSG_ID(msg.id, frame.id);
    } else {
        BM_SET_STD_MSG_ID(msg.id, frame.id);
    }
    if (len > 0)
        std::memcpy(msg.payload, frame.data.constData(), static_cast<size_t>(len));

    const BM_StatusTypeDef st =
        a.WriteCanMessage(static_cast<BM_ChannelHandle>(m_handle), &msg, 0, 0,
                          nullptr);
    return (st == BM_ERROR_OK) ? 1 : -1;
}

int CanDeviceBusmust::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
    if (!m_opened || !m_handle)
        return 0;
    Api &a = api();

    BM_NotificationHandle note = nullptr;
    if (timeoutMs > 0 && a.GetNotification && a.WaitForNotifications) {
        if (a.GetNotification(static_cast<BM_ChannelHandle>(m_handle), &note)
            == BM_ERROR_OK && note) {
            a.WaitForNotifications(&note, 1, timeoutMs);
        } else {
            QThread::msleep(static_cast<unsigned long>(qMin(timeoutMs, 5)));
        }
    }

    // Drain RX queue (one notification may cover multiple messages)
    for (int i = 0; i < 256; ++i) {
        BM_CanMessageTypeDef msg;
        std::memset(&msg, 0, sizeof(msg));
        uint32_t ch = 0;
        uint32_t ts = 0;
        const BM_StatusTypeDef st = a.ReadCanMessage(
            static_cast<BM_ChannelHandle>(m_handle), &msg, &ch, &ts);
        if (st == BM_ERROR_QRCVEMPTY)
            break;
        if (st != BM_ERROR_OK)
            break;

        CanFrame f;
        f.id = msgIdValue(msg);
        f.extended = msg.ctrl.rx.IDE != 0;
        f.fd = msg.ctrl.rx.FDF != 0;
        f.dlc = static_cast<quint8>(msg.ctrl.rx.DLC);
        const int len = dlcToLen(msg.ctrl.rx.DLC, f.fd);
        f.data = QByteArray(reinterpret_cast<const char *>(msg.payload), len);
        // BM timestamp unit is typically microseconds on USB analyzers
        f.timestampNs = static_cast<quint64>(ts) * 1000ull;
        outFrames.push_back(std::move(f));
    }
    return static_cast<int>(outFrames.size());
}

int CanDeviceBusmust::pendingCount() const
{
    return 0;
}

QString CanDeviceBusmust::deviceName() const
{
    return m_name.isEmpty() ? QStringLiteral("BUSMUST USB-CAN(FD)") : m_name;
}
