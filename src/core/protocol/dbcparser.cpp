#include "dbcparser.h"
#include "core/dbcmanager.h"

DbcParser::DbcParser(QObject *parent)
    : QObject(parent)
{
}

QString DbcParser::parserId() const
{
    return QStringLiteral("dbc");
}

QString DbcParser::displayName() const
{
    return QStringLiteral("DBC 数据库");
}

QString DbcParser::iconPath() const
{
    return QStringLiteral(":/icons/database.svg");
}

QStringList DbcParser::fileExtensions() const
{
    return { QStringLiteral("dbc") };
}

BusDefinitionSet DbcParser::parse(const QString &filePath, QString *error) const
{
    // 直通实现：临时 DbcManager 解析（无状态，不触碰全局已加载状态），
    // 再把 DbcMessage/DbcSignal 映射为统一 BusMessageDef/BusSignalDef（§6.2 表）
    DbcManager mgr;
    if (!mgr.loadDbc(filePath)) {
        if (error)
            *error = QStringLiteral("DBC 解析失败: %1").arg(filePath);
        return {};
    }

    BusDefinitionSet set;
    set.parserId = parserId();
    set.filePath = filePath;
    set.protocolHint = QStringLiteral("can");
    for (const auto &f : mgr.files()) {
        for (const auto &m : f.messages) {
            BusMessageDef md;
            md.id = m.id;
            md.name = m.name;
            md.length = m.dlc;
            for (const auto &s : m.signalList) {
                BusSignalDef sd;
                sd.name = s.name;
                sd.startBit = s.startBit;
                sd.bitLength = s.bitLength;
                sd.littleEndian = s.littleEndian;
                sd.factor = s.factor;
                sd.offset = s.offset;
                sd.unit = s.unit;
                sd.comment = s.comment;
                md.signalList.append(sd);
            }
            set.messages.append(md);
        }
    }
    return set;
}
