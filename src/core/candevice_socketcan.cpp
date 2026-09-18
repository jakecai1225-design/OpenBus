#include "candevice_socketcan.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <cstring>

#if defined(Q_OS_LINUX)
#  include <cerrno>
#  include <fcntl.h>
#  include <linux/can.h>
#  include <linux/can/raw.h>
#  include <net/if.h>
#  include <poll.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace {

#if defined(Q_OS_LINUX)
constexpr int kArphrdCan = 280;
constexpr int kCanFdMtu = 72;
constexpr int kCanMtu = 16;

bool isCanIface(const QString &name)
{
    if (name.startsWith(QLatin1String("can"))
        || name.startsWith(QLatin1String("vcan"))
        || name.startsWith(QLatin1String("slcan")))
        return true;
    QFile typeFile(QStringLiteral("/sys/class/net/%1/type").arg(name));
    if (!typeFile.open(QIODevice::ReadOnly))
        return false;
    const int type = typeFile.readAll().trimmed().toInt();
    return type == kArphrdCan;
}

bool ifaceSupportsFd(const QString &name)
{
    QFile mtuFile(QStringLiteral("/sys/class/net/%1/mtu").arg(name));
    if (!mtuFile.open(QIODevice::ReadOnly))
        return false;
    return mtuFile.readAll().trimmed().toInt() >= kCanFdMtu;
}

QString ifIndexToName(int ifIndex)
{
    char buf[IF_NAMESIZE] = {};
    if (if_indextoname(unsigned(ifIndex), buf))
        return QString::fromLocal8Bit(buf);
    return {};
}
#endif

} // namespace

CanDeviceSocketCan::CanDeviceSocketCan(int ifIndex)
    : m_preferredIfIndex(ifIndex)
{
}

CanDeviceSocketCan::~CanDeviceSocketCan()
{
    close();
}

bool CanDeviceSocketCan::isAvailable()
{
#if defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

std::vector<ICanDevice::DeviceInfo> CanDeviceSocketCan::enumerate()
{
    std::vector<DeviceInfo> out;
#if defined(Q_OS_LINUX)
    const QDir net(QStringLiteral("/sys/class/net"));
    const QStringList names = net.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (!isCanIface(name))
            continue;
        const unsigned idx = if_nametoindex(name.toLocal8Bit().constData());
        if (idx == 0)
            continue;

        DeviceInfo di;
        di.brand = Brand::SocketCan;
        di.deviceType = int(idx);
        di.deviceIndex = int(out.size());
        di.channels = 1;
        di.driverId = QStringLiteral("socketcan");
        di.hasHwTimestamp = false;

        QString label = name;
        const bool fd = ifaceSupportsFd(name);
        if (fd)
            label += QStringLiteral(" (FD)");
        if (name.startsWith(QLatin1String("vcan")))
            label += QStringLiteral(" [virtual]");
        di.name = label;
        out.push_back(di);
    }
#else
    // Windows / other: empty list (stub)
#endif
    return out;
}

bool CanDeviceSocketCan::open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd)
{
#if !defined(Q_OS_LINUX)
    Q_UNUSED(devIndex);
    Q_UNUSED(channel);
    Q_UNUSED(arbBaud);
    Q_UNUSED(dataBaud);
    Q_UNUSED(canFd);
    OPENBUS_LOG_ERROR("SocketCAN", "Linux-only backend");
    return false;
#else
    if (m_opened)
        close();

    m_channel = channel < 0 ? 0 : channel;

    int ifIndex = m_preferredIfIndex;
    QString ifName;
    if (ifIndex > 0) {
        ifName = ifIndexToName(ifIndex);
    } else {
        const auto attached = enumerate();
        if (devIndex >= 0 && devIndex < int(attached.size())) {
            ifIndex = attached[size_t(devIndex)].deviceType;
            ifName = attached[size_t(devIndex)].name.split(QLatin1Char(' ')).value(0);
        } else if (!attached.empty()) {
            ifIndex = attached.front().deviceType;
            ifName = attached.front().name.split(QLatin1Char(' ')).value(0);
        }
        if (ifName.isEmpty() && ifIndex > 0)
            ifName = ifIndexToName(ifIndex);
    }
    if (ifName.isEmpty() || ifIndex <= 0) {
        OPENBUS_LOG_ERROR("SocketCAN", "no CAN interface found");
        return false;
    }
    m_ifName = ifName;
    m_preferredIfIndex = ifIndex;

    const int s = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        OPENBUS_LOG_ERROR("SocketCAN", "socket(PF_CAN) failed: errno={}", errno);
        return false;
    }

    m_canFd = canFd && ifaceSupportsFd(ifName);
    if (canFd && !m_canFd) {
        OPENBUS_LOG_WARN("SocketCAN",
            "{} MTU < {}; opening classic CAN (raise MTU / use canfd bitrate)",
            ifName.toStdString(), kCanFdMtu);
    }
    if (m_canFd) {
        const int enable = 1;
        if (::setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable, sizeof(enable)) != 0) {
            OPENBUS_LOG_WARN("SocketCAN",
                "CAN_RAW_FD_FRAMES unsupported; classic only (errno={})", errno);
            m_canFd = false;
        }
    }

    // Non-blocking reads are driven by poll() in recv().
    const int flags = ::fcntl(s, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(s, F_SETFL, flags | O_NONBLOCK);

    sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifIndex;
    if (::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        OPENBUS_LOG_ERROR("SocketCAN", "bind({}) failed: errno={}",
                          ifName.toStdString(), errno);
        ::close(s);
        return false;
    }

    m_fd = s;
    m_opened = true;
    OPENBUS_LOG_INFO("SocketCAN",
        "opened {} (ifindex={}, arb={}, data={}, fd={}) — bitrate is OS-managed "
        "(ip link set … type can bitrate)",
        ifName.toStdString(), ifIndex, arbBaud, dataBaud, m_canFd);
    Q_UNUSED(arbBaud);
    Q_UNUSED(dataBaud);
    return true;
#endif
}

void CanDeviceSocketCan::close()
{
#if defined(Q_OS_LINUX)
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    if (m_opened)
        OPENBUS_LOG_INFO("SocketCAN", "closed {}", m_ifName.toStdString());
    m_opened = false;
}

int CanDeviceSocketCan::send(const CanFrame &frame)
{
#if !defined(Q_OS_LINUX)
    Q_UNUSED(frame);
    return 0;
#else
    if (!m_opened || m_fd < 0)
        return 0;

    if (m_canFd && (frame.fd || frame.data.size() > 8 || frame.bitrateSwitch)) {
        canfd_frame cf;
        std::memset(&cf, 0, sizeof(cf));
        cf.can_id = frame.id & 0x1FFFFFFFu;
        if (frame.extended)
            cf.can_id |= CAN_EFF_FLAG;
        const int nbytes = qMin(64, frame.data.size());
        cf.len = static_cast<__u8>(nbytes);
        cf.flags = 0;
        if (frame.bitrateSwitch)
            cf.flags |= CANFD_BRS;
        if (frame.errorState)
            cf.flags |= CANFD_ESI;
        if (nbytes > 0)
            std::memcpy(cf.data, frame.data.constData(), size_t(nbytes));
        const ssize_t n = ::write(m_fd, &cf, sizeof(cf));
        return (n == ssize_t(sizeof(cf))) ? 1 : 0;
    }

    can_frame cf;
    std::memset(&cf, 0, sizeof(cf));
    cf.can_id = frame.id & 0x1FFFFFFFu;
    if (frame.extended)
        cf.can_id |= CAN_EFF_FLAG;
    const int nbytes = qMin(8, frame.data.size());
    cf.can_dlc = static_cast<__u8>(nbytes);
    if (nbytes > 0)
        std::memcpy(cf.data, frame.data.constData(), size_t(nbytes));
    const ssize_t n = ::write(m_fd, &cf, sizeof(cf));
    return (n == ssize_t(sizeof(cf))) ? 1 : 0;
#endif
}

int CanDeviceSocketCan::recv(int timeoutMs, std::vector<CanFrame> &outFrames)
{
    outFrames.clear();
#if !defined(Q_OS_LINUX)
    Q_UNUSED(timeoutMs);
    return 0;
#else
    if (!m_opened || m_fd < 0)
        return 0;

    pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int to = timeoutMs < 0 ? -1 : timeoutMs;
    if (::poll(&pfd, 1, to) <= 0)
        return 0;

    while (outFrames.size() < 256) {
        unsigned char buf[kCanFdMtu];
        const ssize_t n = ::read(m_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            OPENBUS_LOG_DEBUG("SocketCAN", "read errno={}", errno);
            break;
        }
        if (n == 0)
            break;

        CanFrame frame;
        frame.channel = static_cast<quint8>(m_channel + 1);
        frame.direction = CanFrame::Rx;

        if (n == kCanFdMtu || (m_canFd && n > kCanMtu)) {
            canfd_frame cf;
            std::memset(&cf, 0, sizeof(cf));
            std::memcpy(&cf, buf, size_t(qMin(n, ssize_t(sizeof(cf)))));
            frame.id = cf.can_id & CAN_EFF_MASK;
            frame.extended = (cf.can_id & CAN_EFF_FLAG) != 0;
            frame.fd = true;
            frame.bitrateSwitch = (cf.flags & CANFD_BRS) != 0;
            frame.errorState = (cf.flags & CANFD_ESI) != 0;
            if (cf.can_id & CAN_ERR_FLAG)
                frame.id |= 0x20000000u;
            const int nbytes = int(qMin(64, int(cf.len)));
            frame.dlc = CanFrame::lengthToDlc(nbytes);
            frame.data = QByteArray(reinterpret_cast<const char *>(cf.data), nbytes);
        } else {
            can_frame cf;
            std::memset(&cf, 0, sizeof(cf));
            std::memcpy(&cf, buf, size_t(qMin(n, ssize_t(sizeof(cf)))));
            frame.id = cf.can_id & CAN_EFF_MASK;
            frame.extended = (cf.can_id & CAN_EFF_FLAG) != 0;
            if (cf.can_id & CAN_ERR_FLAG)
                frame.id |= 0x20000000u;
            const int nbytes = int(qMin(8, int(cf.can_dlc)));
            frame.dlc = CanFrame::lengthToDlc(nbytes);
            frame.data = QByteArray(reinterpret_cast<const char *>(cf.data), nbytes);
        }
        outFrames.push_back(std::move(frame));

        // Drain remaining without blocking
        pfd.revents = 0;
        if (::poll(&pfd, 1, 0) <= 0)
            break;
    }
    return static_cast<int>(outFrames.size());
#endif
}

int CanDeviceSocketCan::pendingCount() const
{
#if !defined(Q_OS_LINUX)
    return 0;
#else
    if (!m_opened || m_fd < 0)
        return 0;
    pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return (::poll(&pfd, 1, 0) > 0) ? 1 : 0;
#endif
}

QString CanDeviceSocketCan::deviceName() const
{
    return m_ifName.isEmpty() ? QStringLiteral("SocketCAN") : m_ifName;
}
