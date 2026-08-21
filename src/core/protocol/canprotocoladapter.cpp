#include "canprotocoladapter.h"
#include "core/dbcmanager.h"
#include "utils/canutils.h"

CanProtocolAdapter::CanProtocolAdapter(QObject *parent)
    : QObject(parent)
{
}

QString CanProtocolAdapter::protocolId() const
{
    return QStringLiteral("can");
}

QString CanProtocolAdapter::displayName() const
{
    return QStringLiteral("CAN Flow");
}

QString CanProtocolAdapter::iconPath() const
{
    // M2 复用现有图标资源；协议专属图标（:/icons/protocols/can.svg）随 F4 市场补
    return QStringLiteral(":/icons/flow.svg");
}

QStringList CanProtocolAdapter::supportedSources() const
{
    // 硬件（CanDeviceManager）+ 文件（Player/canfileio）+ 仿真（CanSimulator）
    return { QStringLiteral("hardware"), QStringLiteral("file"), QStringLiteral("simulator") };
}

int CanProtocolAdapter::maxChannels() const
{
    return 16;
}

QStringList CanProtocolAdapter::acceptedParsers() const
{
    // M2 仅 DBC 直通；ARXML / J1939 DBC 随 F1 剩余接入（§13.5 不做清单）
    return { QStringLiteral("dbc") };
}

QList<IProtocolAdapter::DecodedSignal> CanProtocolAdapter::decode(const BusMessage &msg) const
{
    QList<DecodedSignal> out;
    if (!m_dbcManager || msg.bus != BusType::Can)
        return out;
    // 复用 DbcManager 解码路径（findMessage → signalList 解码）
    const auto decoded = m_dbcManager->decodeFrame(msg.id, msg.payload);
    out.reserve(decoded.size());
    for (const auto &d : decoded) {
        DecodedSignal s;
        s.name = d.name;
        s.value = d.physValue;
        s.unit = d.unit;
        s.raw = d.valueDesc;   // 值表描述占用 raw 展示位（含义优先于原始值）
        out.append(s);
    }
    return out;
}

QList<IProtocolAdapter::TraceColumnDef> CanProtocolAdapter::traceColumns() const
{
    // 与 CanTraceModel::Columns 现有 12 列一一对应（M2 仅供注册表输出；
    // 列模型接线为 F1 剩余，§13.5）
    return {
        { QStringLiteral("no"),     QStringLiteral("No."),     60 },
        { QStringLiteral("time"),   QStringLiteral("Time"),     90 },
        { QStringLiteral("delta"),  QStringLiteral("Delta"),    70 },
        { QStringLiteral("ch"),     QStringLiteral("Ch"),       36 },
        { QStringLiteral("dir"),    QStringLiteral("Dir"),      36 },
        { QStringLiteral("id"),     QStringLiteral("ID"),       76 },
        { QStringLiteral("name"),   QStringLiteral("Name"),     110 },
        { QStringLiteral("dlc"),    QStringLiteral("DLC"),      40 },
        { QStringLiteral("data"),   QStringLiteral("Data"),     210 },
        { QStringLiteral("flags"),  QStringLiteral("Flags"),    60 },
        { QStringLiteral("count"),  QStringLiteral("Count"),    56 },
        { QStringLiteral("signal"), QStringLiteral("Signals"),  180 },
    };
}

QString CanProtocolAdapter::formatField(const BusMessage &msg, const QString &key) const
{
    // 复用 CanUtils 既有格式化（与 Trace 视图现行显示一致）
    const CanFrame f = toCanFrame(msg);
    if (key == QLatin1String("id"))
        return CanUtils::formatId(msg.id, (msg.flags & BusMessageFlags::CanExtended) != 0);
    if (key == QLatin1String("data"))
        return CanUtils::formatData(msg.payload);
    if (key == QLatin1String("dlc"))
        return CanUtils::formatDlc(f.dlc, (msg.flags & BusMessageFlags::CanFd) != 0);
    if (key == QLatin1String("flags"))
        return CanUtils::formatFlags(f);
    if (key == QLatin1String("ch"))
        return QString::number(msg.channel);
    if (key == QLatin1String("dir"))
        return (msg.flags & BusMessageFlags::DirectionTx) ? QStringLiteral("Tx")
                                                          : QStringLiteral("Rx");
    if (key == QLatin1String("time"))
        return CanUtils::formatTime(msg.timestampNs / 1e9);
    return {};
}

QStringList CanProtocolAdapter::fileFilters() const
{
    return { QStringLiteral("*.blf"), QStringLiteral("*.asc"), QStringLiteral("*.sin") };
}
