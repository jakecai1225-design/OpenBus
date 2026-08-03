#include "dbc_writer.h"

#include <QFile>
#include <QTextStream>
#include <QStringConverter>

namespace dbc {

static QString escString(const QString &s)
{
    // DBC 字符串中需要转义的双引号
    QString r = s;
    r.replace('\\', "\\\\");
    r.replace('"', "\\\"");
    return r;
}

static QString formatSignalLine(const DbcSignal &sig)
{
    // SG_ Name : startBit|bitLen@endianSign (factor,offset) [min|max] "unit" receiver
    QString line = QStringLiteral("SG_ %1 : %2|%3@%4%5 (%6,%7) [%8|%9] \"%10\" %11");

    // endianSign: 1=Intel(little), 0=Motorola(big)
    QString endianSign = sig.littleEndian ? QStringLiteral("1") : QStringLiteral("0");
    QString signChar = sig.isSigned ? QStringLiteral("-") : QStringLiteral("+");

    // mux 标记
    QString nameWithMux = sig.name;
    if (sig.muxType == DbcSignal::MuxType::Multiplexor) {
        nameWithMux += QStringLiteral(" M");
    } else if (sig.muxType == DbcSignal::MuxType::Multiplexed) {
        nameWithMux += QStringLiteral(" m%1").arg(sig.muxValue);
    }

    return line.arg(nameWithMux)
        .arg(sig.startBit)
        .arg(sig.bitLength)
        .arg(endianSign, signChar)
        .arg(sig.factor)
        .arg(sig.offset)
        .arg(sig.minimum)
        .arg(sig.maximum)
        .arg(escString(sig.unit))
        .arg(sig.receiver.isEmpty() ? QStringLiteral("Vector__XXX") : sig.receiver);
}

bool write(const QString &filePath, const DbcFile &file)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);

    // VERSION
    ts << "VERSION \""
       << (file.version.isEmpty() ? QStringLiteral("sin DBC editor") : escString(file.version))
       << "\"\n";

    // NS_ (new symbols)
    ts << "\nNS_ :\n";
    const QStringList nsKeys = {
        "NS_DESC_", "CM_", "BA_DEF_", "BA_", "VAL_", "CAT_DEF_", "CAT_", "FILTER",
        "BA_DEF_DEF_", "EV_DATA_", "ENVVAR_DATA_", "SGTYPE_", "SGTYPE_VAL_", "BA_DEF_SGTYPE_",
        "BA_SGTYPE_", "SIG_TYPE_REF_", "VAL_TABLE_", "SIG_GROUP_", "SIG_VALTYPE_",
        "SIGTYPE_VALTYPE_", "BO_TX_BU_", "BA_DEF_REL_", "BA_REL_", "BA_DEF_DEF_REL_",
        "BA_REL_DEFAULT_", "BA_DEF_SGTYPE_REL_", "BA_SGTYPE_REL_", "BA_DEF_SETDATA_REL_",
        "BA_SETDATA_REL_", "BA_DEF_SETDATA_", "BA_SETDATA_", "STR_64", "BA_SETDATA_REF_", "BA_SETDATA_SGTYPE_"
    };
    for (const auto &k : nsKeys)
        ts << "    " << k << "\n";
    ts << "\n";

    // BS_
    ts << "BS_:\n";
    ts << "\n";

    // BU_
    ts << "BU_:";
    for (const auto &node : file.nodes)
        ts << " " << node.name;
    ts << "\n\n";

    // BO_ + SG_
    for (const auto &msg : file.messages) {
        ts << QStringLiteral("BO_ %1 %2: %3 %4\n")
            .arg(msg.id).arg(msg.name).arg(msg.dlc).arg(msg.sender.isEmpty() ? QStringLiteral("Vector__XXX") : msg.sender);
        for (const auto &sig : msg.signalList) {
            ts << "    " << formatSignalLine(sig) << "\n";
        }
        ts << "\n";
    }

    // VAL_TABLE_
    for (const auto &vt : file.valueTables) {
        ts << QStringLiteral("VAL_TABLE_ %1 ").arg(vt.name);
        for (const auto &e : vt.entries)
            ts << e.value << " \"" << escString(e.description) << "\" ";
        ts << ";\n";
    }
    if (!file.valueTables.isEmpty())
        ts << "\n";

    // CM_ comments
    for (const auto &node : file.nodes) {
        if (!node.comment.isEmpty())
            ts << QStringLiteral("CM_ BU_ %1 \"%2\";\n").arg(node.name, escString(node.comment));
    }
    for (const auto &msg : file.messages) {
        if (!msg.comment.isEmpty())
            ts << QStringLiteral("CM_ BO_ %1 \"%2\";\n").arg(msg.id).arg(escString(msg.comment));
        for (const auto &sig : msg.signalList) {
            if (!sig.comment.isEmpty())
                ts << QStringLiteral("CM_ SG_ %1 %2 \"%3\";\n")
                    .arg(msg.id).arg(sig.name).arg(escString(sig.comment));
        }
    }

    // BA_DEF_ + BA_DEF_DEF_ — 仅写入已存在的属性定义
    for (const auto &def : file.attributeDefs) {
        ts << QStringLiteral("BA_DEF_ ");
        switch (def.scope) {
        case DbcAttributeDef::Scope::Node:     ts << "BU_ "; break;
        case DbcAttributeDef::Scope::Message:  ts << "BO_ "; break;
        case DbcAttributeDef::Scope::Signal:   ts << "SG_ "; break;
        case DbcAttributeDef::Scope::Network:  break;
        }
        ts << "\"" << def.name << "\" ";

        switch (def.dataType) {
        case DbcAttributeDef::DataType::Int:
            ts << "INT";
            if (!def.minValue.isNull() && !def.maxValue.isNull())
                ts << " " << def.minValue.toInt() << " " << def.maxValue.toInt();
            break;
        case DbcAttributeDef::DataType::Float:
            ts << "FLOAT";
            if (!def.minValue.isNull() && !def.maxValue.isNull())
                ts << " " << def.minValue.toDouble() << " " << def.maxValue.toDouble();
            break;
        case DbcAttributeDef::DataType::String:
            ts << "CHAR";
            break;
        case DbcAttributeDef::DataType::Enum:
            ts << "ENUM";
            for (const auto &choice : def.enumChoices)
                ts << ",\"" << escString(choice) << "\"";
            break;
        }
        ts << ";\n";

        // BA_DEF_DEF_
        if (!def.defaultValue.isNull()) {
            ts << "BA_DEF_DEF_ \"" << def.name << "\" ";
            if (def.dataType == DbcAttributeDef::DataType::String ||
                def.dataType == DbcAttributeDef::DataType::Enum) {
                ts << "\"" << escString(def.defaultValue.toString()) << "\"";
            } else {
                ts << def.defaultValue.toString();
            }
            ts << ";\n";
        }
    }

    // BA_ — 属性值
    // scope 由字段推断: signalName→SG_, canId→BO_, nodeName→BU_, else→Network
    for (const auto &av : file.attributeValues) {
        ts << "BA_ \"" << av.attributeName << "\"";
        if (!av.signalName.isEmpty() && av.canId != 0) {
            ts << " SG_ " << av.canId << " " << av.signalName;
        } else if (av.canId != 0) {
            ts << " BO_ " << av.canId;
        } else if (!av.nodeName.isEmpty()) {
            ts << " BU_ " << av.nodeName;
        }
        ts << " ";
        // 值类型判断
        QVariant v = av.value;
        if (v.typeId() == QMetaType::QString)
            ts << "\"" << escString(v.toString()) << "\"";
        else
            ts << v.toString();
        ts << ";\n";
    }

    // VAL_ — 信号值描述（内联值表）
    for (const auto &msg : file.messages) {
        for (const auto &sig : msg.signalList) {
            if (sig.valueTable.isEmpty()) continue;
            ts << QStringLiteral("VAL_ %1 %2 ").arg(msg.id).arg(sig.name);
            for (const auto &e : sig.valueTable)
                ts << e.value << " \"" << escString(e.description) << "\" ";
            ts << ";\n";
        }
    }

    // SIG_VALTYPE_
    for (const auto &msg : file.messages) {
        for (const auto &sig : msg.signalList) {
            if (sig.extendedValueType == DbcSignal::ExtendedValueType::Float)
                ts << QStringLiteral("SIG_VALTYPE_ %1 %2 : 1;\n").arg(msg.id).arg(sig.name);
            else if (sig.extendedValueType == DbcSignal::ExtendedValueType::Double)
                ts << QStringLiteral("SIG_VALTYPE_ %1 %2 : 2;\n").arg(msg.id).arg(sig.name);
        }
    }

    // BO_TX_BU_
    for (const auto &msg : file.messages) {
        if (!msg.txNodes.isEmpty()) {
            ts << QStringLiteral("BO_TX_BU_ %1 :").arg(msg.id);
            for (const auto &n : msg.txNodes)
                ts << ", " << n;
            ts << ";\n";
        }
    }

    ts.flush();
    f.close();
    return true;
}

} // namespace dbc
