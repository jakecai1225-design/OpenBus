#ifndef BUSMESSAGE_H
#define BUSMESSAGE_H

#include "canframe.h"

#include <QByteArray>
#include <QMetaType>
#include <cstdint>

/**
 * @file busmessage.h
 * @brief 统一总线报文结构（doc/flow.md §四；M2 预埋，纯新增不接线）
 *
 * 多协议方案的数据层公共载体：所有总线（CAN / EtherCAT / General / 第三方）
 * 的报文在通用层以该扁平结构表示。M2 仅落地结构与 CAN 转换函数，
 * 热路径仍走既有 onFrameReceived(CanFrame)（§13.5 不做清单）。
 */

/**
 * @brief 总线类型（协议大类）
 *
 * 取值即持久化 / 注册表命名空间键，一经发布不再改动（只增不改纪律，R7）。
 */
enum class BusType : quint8 {
    Unknown = 0,   ///< 未知 / 未设置
    Can = 1,       ///< CAN / CAN FD
    Ethercat = 2,  ///< EtherCAT（F3）
    General = 3,   ///< 通用字节流（F2）
};

/**
 * @brief CAN 适配器复用的 flags 位布局（与 canframe.h 序列化位一致）
 *
 * 语义无损迁移：BusMessage.flags 在 CAN 协议下直接沿用 CanFrame 的
 * 既有位约定；其它协议的 flags 位语义由各适配器自行定义。
 */
namespace BusMessageFlags {
constexpr quint32 CanExtended = 0x01;  ///< 扩展帧（29 位 ID）
constexpr quint32 CanFd = 0x02;        ///< CAN FD 帧
constexpr quint32 CanBrs = 0x04;       ///< CAN FD BRS
constexpr quint32 CanEsi = 0x08;       ///< CAN FD ESI
constexpr quint32 DirectionTx = 0x10;  ///< 0=Rx 1=Tx
}

/**
 * @brief 统一总线报文（扁平结构）
 *
 * 设计取舍（§4.1）：
 * - 无 QVariantMap 热路径字段——协议扩展信息一律由适配器按需从扁平字段解码；
 * - QByteArray payload 隐式共享，拷贝成本与 CanFrame 相当；
 * - id 语义由适配器解释（CAN=标识符 / EtherCAT=datagram 命令字 / General=流 ID）。
 */
struct BusMessage
{
    quint64 timestampNs = 0;   ///< 全局单调纳秒时间戳（多总线统一时间轴）
    BusType bus = BusType::Unknown;
    quint8 channel = 0;        ///< 协议内通道号（1-based；语义由适配器定义）
    quint8 direction = 0;      ///< 0=Rx 1=Tx
    quint32 id = 0;            ///< 协议内标识
    quint32 flags = 0;         ///< 位标志；bit 语义由适配器定义（CAN 复用 CanFrame 位）
    QByteArray payload;        ///< 原始数据（CAN 0-64B / General 任意长）
};

Q_DECLARE_METATYPE(BusMessage)

/**
 * @brief CanFrame → BusMessage（ingestion 边界转换，§4.3-1）
 *
 * dlc 不迁移（可由 payload.size() 经 CanFrame::lengthToDlc 还原）；
 * timestamp 兼容字段不迁移（由 timestampNs 派生）。
 */
inline BusMessage toBusMessage(const CanFrame &f)
{
    BusMessage m;
    m.timestampNs = f.timestampNs;
    m.bus = BusType::Can;
    m.channel = f.channel;
    m.direction = f.direction == CanFrame::Tx ? 1 : 0;
    m.id = f.id;
    m.flags = (f.extended      ? BusMessageFlags::CanExtended : 0u) |
              (f.fd            ? BusMessageFlags::CanFd       : 0u) |
              (f.bitrateSwitch ? BusMessageFlags::CanBrs      : 0u) |
              (f.errorState    ? BusMessageFlags::CanEsi      : 0u) |
              (f.direction     ? BusMessageFlags::DirectionTx : 0u);
    m.payload = f.data;
    return m;
}

/**
 * @brief BusMessage → CanFrame（egress 边界转换，§4.3-1）
 *
 * bus != Can 的报文调用本函数无定义（调用方保证）；timestamp 由
 * timestampNs 派生以保持既有兼容字段有效。
 */
inline CanFrame toCanFrame(const BusMessage &m)
{
    CanFrame f;
    f.timestamp = m.timestampNs / 1e9;
    f.timestampNs = m.timestampNs;
    f.id = m.id;
    f.extended = (m.flags & BusMessageFlags::CanExtended) != 0;
    f.fd = (m.flags & BusMessageFlags::CanFd) != 0;
    f.bitrateSwitch = (m.flags & BusMessageFlags::CanBrs) != 0;
    f.errorState = (m.flags & BusMessageFlags::CanEsi) != 0;
    f.direction = (m.flags & BusMessageFlags::DirectionTx) ? CanFrame::Tx : CanFrame::Rx;
    f.dlc = CanFrame::lengthToDlc(m.payload.size());
    f.channel = m.channel;
    f.data = m.payload;
    return f;
}

#endif // BUSMESSAGE_H
