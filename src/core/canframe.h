#ifndef CANFRAME_H
#define CANFRAME_H

#include <QByteArray>
#include <QMetaType>
#include <QVector>
#include <cstdint>

/**
 * @brief CAN / CAN FD 帧数据结构
 *
 * 表示一个 CAN 经典帧或 CAN FD 帧，包含时间戳、ID、数据等完整信息。
 * 用于 Trace 显示、录制、回放、Graphic 绘制等全流程。
 */
struct CanFrame
{
    enum Direction { Rx = 0, Tx = 1 };

    /// 相对开始时间（秒），精度到微秒（向后兼容字段，由 timestampNs 派生）
    double timestamp = 0.0;

    /// 纳秒级时间戳（相对测量开始的单调时钟纳秒数，0=未设置）
    /// 由 CanDeviceManager 统一使用 std::chrono::steady_clock 填充，
    /// 确保多设备时间轴一致，避免厂商时钟差异
    quint64 timestampNs = 0;

    /// CAN ID（标准 11 位 / 扩展 29 位）
    quint32 id = 0;

    /// 是否扩展帧（29 位 ID）
    bool extended = false;

    /// 是否 CAN FD 帧
    bool fd = false;

    /// CAN FD BRS (Bit Rate Switch)
    bool bitrateSwitch = false;

    /// CAN FD ESI (Error State Indicator)
    bool errorState = false;

    /// 数据长度码（DLC），CAN FD 下 0-15 对应 0-64 字节
    quint8 dlc = 0;

    /// 实际数据（0-64 字节）
    QByteArray data;

    /// 通道号（1-based）
    quint8 channel = 1;

    /// 方向（接收 / 发送）
    Direction direction = Rx;

    // ---- DLC 工具函数 ----

    /// DLC 码 -> 实际字节数
    static int dlcToLength(quint8 dlc)
    {
        static const int table[16] = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
        };
        return table[dlc & 0x0F];
    }

    /// 实际字节数 -> DLC 码
    static quint8 lengthToDlc(int length)
    {
        if (length <= 8)  return static_cast<quint8>(length);
        if (length <= 12) return 9;
        if (length <= 16) return 10;
        if (length <= 20) return 11;
        if (length <= 24) return 12;
        if (length <= 32) return 13;
        if (length <= 48) return 14;
        return 15;
    }

    /// 实际数据长度（基于 data.size()）
    int length() const { return data.size(); }

    /// 是否为错误帧
    bool isErrorFrame() const
    {
        return (id & 0x20000000) != 0; // CAN_ERR_FLAG
    }

    /// 序列化为 QDataStream（用于录制 / 回放文件）
    friend QDataStream &operator<<(QDataStream &out, const CanFrame &f)
    {
        out << f.timestamp;
        out << static_cast<quint64>(f.timestampNs);
        out << static_cast<quint32>(f.id);
        out << static_cast<quint8>(
            (f.extended      ? 0x01 : 0) |
            (f.fd            ? 0x02 : 0) |
            (f.bitrateSwitch ? 0x04 : 0) |
            (f.errorState    ? 0x08 : 0) |
            (f.direction     ? 0x10 : 0));
        out << f.dlc;
        out << f.channel;
        out << f.data;
        return out;
    }

    /// 从 QDataStream 反序列化
    friend QDataStream &operator>>(QDataStream &in, CanFrame &f)
    {
        quint32 id;
        quint8 flags;
        quint64 tsNs;
        in >> f.timestamp >> tsNs >> id >> flags >> f.dlc >> f.channel >> f.data;
        f.timestampNs = tsNs;
        f.id = id;
        f.extended      = (flags & 0x01) != 0;
        f.fd            = (flags & 0x02) != 0;
        f.bitrateSwitch = (flags & 0x04) != 0;
        f.errorState    = (flags & 0x08) != 0;
        f.direction     = static_cast<Direction>((flags & 0x10) ? 1 : 0);
        return in;
    }
};

Q_DECLARE_METATYPE(CanFrame)
Q_DECLARE_METATYPE(QVector<CanFrame>)

#endif // CANFRAME_H
