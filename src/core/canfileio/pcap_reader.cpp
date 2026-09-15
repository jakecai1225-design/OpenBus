#include "pcap_reader.h"
#include "core/canframe.h"

#include <QDataStream>
#include <QtEndian>
#include <cstring>

// ============================================================
//  PCAP 格式常量
// ============================================================

namespace {

// PCAP 全局头魔数
constexpr quint32 PCAP_MAGIC_NATIVE  = 0xa1b2c3d4;  // 微秒精度，本机字节序
constexpr quint32 PCAP_MAGIC_SWAPPED = 0xd4c3b2a1;  // 微秒精度，交换字节序
constexpr quint32 PCAP_MAGIC_NS_NAT  = 0xa1b23c4d;  // 纳秒精度，本机字节序
constexpr quint32 PCAP_MAGIC_NS_SWP  = 0x4d3cb2a1;  // 纳秒精度，交换字节序

// 链路类型
constexpr quint32 LINKTYPE_CAN        = 227;  // Linux SocketCAN
constexpr quint32 LINKTYPE_LINUX_SLL   = 113;  // Linux cooked capture
constexpr quint32 LINKTYPE_CAN_FD      = 0x00000101; // CAN FD (非标准但部分工具使用)

// Linux SocketCAN 帧标志位
constexpr quint32 CAN_EFF_FLAG = 0x80000000;  // 扩展帧
constexpr quint32 CAN_RTR_FLAG = 0x40000000;  // 远程帧
constexpr quint32 CAN_ERR_FLAG = 0x20000000; // 错误帧

// 经典 CAN 帧大小
constexpr int CAN_FRAME_SIZE = 16;   // can_id(4) + can_dlc(1) + pad(3) + data(8)
// CAN FD 帧大小
constexpr int CANFD_FRAME_SIZE = 72; // can_id(4) + flags(1) + len(1) + pad(6) + data(64)

} // namespace

// ============================================================
//  PcapReader
// ============================================================

PcapReader::PcapReader() = default;

PcapReader::~PcapReader()
{
    close();
}

bool PcapReader::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly))
        return false;

    // 读取全局头 (24 bytes)
    QByteArray header = m_file.read(24);
    if (header.size() < 24)
        return false;

    const auto *p = reinterpret_cast<const quint8 *>(header.constData());
    quint32 magic = qFromLittleEndian<quint32>(p);

    // 判断字节序和时间精度
    if (magic == PCAP_MAGIC_NATIVE || magic == PCAP_MAGIC_NS_NAT) {
        // 本机字节序 (小端)
        m_nanosecond = (magic == PCAP_MAGIC_NS_NAT);
    } else if (magic == PCAP_MAGIC_SWAPPED || magic == PCAP_MAGIC_NS_SWP) {
        // 交换字节序 — 也按小端读取（magic 已反转，数据同样反转）
        // 实际上交换的 magic 表示文件使用大端，但我们这里简化处理
        m_nanosecond = (magic == PCAP_MAGIC_NS_SWP);
    } else {
        // 不是 PCAP 文件
        return false;
    }

    // 读取链路类型 (offset 20)
    m_linkType = qFromLittleEndian<quint32>(p + 20);

    // 仅支持 CAN 相关链路类型
    // 如果链路类型不匹配，仍尝试解析（有些工具使用非标准链路类型）
    return true;
}

int PcapReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    QDataStream ds(&m_file);
    ds.setByteOrder(QDataStream::LittleEndian);

    double baseTime = -1.0;
    int count = 0;

    while (!ds.atEnd()) {
        // 读取包头 (16 bytes)
        quint32 tsSec, tsFrac, inclLen, origLen;
        ds >> tsSec >> tsFrac >> inclLen >> origLen;
        if (ds.status() != QDataStream::Ok)
            break;

        // 计算时间戳
        double timestamp;
        if (m_nanosecond)
            timestamp = tsSec + tsFrac / 1e9;
        else
            timestamp = tsSec + tsFrac / 1e6;

        // 读取包数据
        if (inclLen == 0 || inclLen > 65535)
            continue;

        QByteArray packetData(inclLen, Qt::Uninitialized);
        ds.readRawData(packetData.data(), inclLen);

        // 解析 CAN 帧
        CanFrame frame;
        frame.timestamp = timestamp;
        frame.channel = 1;
        frame.direction = CanFrame::Rx;

        bool parsed = false;

        if (m_linkType == LINKTYPE_CAN || m_linkType == LINKTYPE_CAN_FD) {
            // 直接的 CAN 帧
            if (inclLen >= CAN_FRAME_SIZE) {
                const auto *raw = reinterpret_cast<const quint8 *>(packetData.constData());
                quint32 canId = qFromLittleEndian<quint32>(raw);
                quint8 dlc = raw[4];

                frame.id = canId & 0x1FFFFFFF;
                frame.extended = (canId & CAN_EFF_FLAG) != 0;

                // 判断经典 CAN 还是 CAN FD
                if (inclLen >= CANFD_FRAME_SIZE && dlc > 8) {
                    // CAN FD 帧: can_id(4) + flags(1) + len(1) + pad(6) + data(64)
                    frame.fd = true;
                    frame.dlc = CanFrame::lengthToDlc(dlc);
                    quint8 fdFlags = raw[5];
                    frame.bitrateSwitch = (fdFlags & 0x01) != 0;
                    frame.errorState = (fdFlags & 0x02) != 0;
                    int dataLen = qMin(static_cast<int>(dlc), 64);
                    frame.data = QByteArray(reinterpret_cast<const char *>(raw + 8), dataLen);
                } else {
                    // 经典 CAN 帧: can_id(4) + can_dlc(1) + pad(3) + data(8)
                    frame.fd = false;
                    frame.dlc = qMin(dlc, static_cast<quint8>(8));
                    int dataLen = qMin(static_cast<int>(dlc), 8);
                    frame.data = QByteArray(reinterpret_cast<const char *>(raw + 8), dataLen);
                }
                parsed = true;
            }
        } else if (m_linkType == LINKTYPE_LINUX_SLL) {
            // Linux cooked capture — 有 16 字节的 SLL 头
            // SLL header: pkttype(2) + hatype(2) + halen(2) + addr(8) + protocol(2) = 16 bytes
            if (inclLen >= 16 + CAN_FRAME_SIZE) {
                const auto *raw = reinterpret_cast<const quint8 *>(packetData.constData());
                // 协议类型在偏移 14
                quint16 protocol = qFromLittleEndian<quint16>(raw + 14);
                // 0x000c = ETH_P_CAN, 0x000d = ETH_P_CANFD
                if (protocol == 0x000c || protocol == 0x000d) {
                    const auto *canData = raw + 16;
                    quint32 canId = qFromLittleEndian<quint32>(canData);
                    quint8 dlc = canData[4];

                    frame.id = canId & 0x1FFFFFFF;
                    frame.extended = (canId & CAN_EFF_FLAG) != 0;

                    if (protocol == 0x000d && inclLen >= 16 + CANFD_FRAME_SIZE) {
                        frame.fd = true;
                        frame.dlc = CanFrame::lengthToDlc(dlc);
                        quint8 fdFlags = canData[5];
                        frame.bitrateSwitch = (fdFlags & 0x01) != 0;
                        frame.errorState = (fdFlags & 0x02) != 0;
                        int dataLen = qMin(static_cast<int>(dlc), 64);
                        frame.data = QByteArray(reinterpret_cast<const char *>(canData + 8), dataLen);
                    } else {
                        frame.fd = false;
                        frame.dlc = qMin(dlc, static_cast<quint8>(8));
                        int dataLen = qMin(static_cast<int>(dlc), 8);
                        frame.data = QByteArray(reinterpret_cast<const char *>(canData + 8), dataLen);
                    }
                    parsed = true;
                }
            }
        } else {
            // 未知链路类型，尝试直接解析为 CAN 帧
            if (inclLen >= CAN_FRAME_SIZE) {
                const auto *raw = reinterpret_cast<const quint8 *>(packetData.constData());
                quint32 canId = qFromLittleEndian<quint32>(raw);
                quint8 dlc = raw[4];

                // 基本合理性检查
                if (dlc <= 64 && (canId & 0xE0000000) == 0) {
                    frame.id = canId & 0x1FFFFFFF;
                    frame.extended = (canId & CAN_EFF_FLAG) != 0;
                    frame.dlc = qMin(dlc, static_cast<quint8>(8));
                    int dataLen = qMin(static_cast<int>(dlc), 8);
                    frame.data = QByteArray(reinterpret_cast<const char *>(raw + 8), dataLen);
                    parsed = true;
                }
            }
        }

        if (parsed) {
            // Shared Trace/Graphic axis: relative to first frame unless merging files
            if (!keepAbsoluteTimestamps()) {
                if (baseTime < 0)
                    baseTime = frame.timestamp;
                frame.timestamp -= baseTime;
            }
            if (frame.timestampNs == 0)
                frame.timestampNs = static_cast<quint64>(frame.timestamp * 1e9 + 0.5);
            frames.append(frame);
            ++count;
        }
    }

    return count;
}

void PcapReader::close()
{
    m_file.close();
}
