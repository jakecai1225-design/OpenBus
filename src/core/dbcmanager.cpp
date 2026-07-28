#include "dbcmanager.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

// ============================================================
//  DbcSignal::decode
// ============================================================

double DbcSignal::decode(const QByteArray &data) const
{
    if (data.isEmpty())
        return 0.0;

    int byteCount = (bitLength + 7) / 8;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(data.constData());

    quint64 raw = 0;

    if (littleEndian) {
        // Intel byte order
        int startByte = startBit / 8;
        int startBitInByte = startBit % 8;
        for (int i = 0; i < byteCount && (startByte + i) < data.size(); ++i) {
            raw |= static_cast<quint64>(p[startByte + i]) << (8 * i);
        }
        raw >>= startBitInByte;
    } else {
        // Motorola byte order (big endian)
        int startByte = startBit / 8;
        int bitPos = startBit;
        for (int i = 0; i < bitLength; ++i) {
            int byteIdx = startByte - (bitPos / 8);
            int bitIdx = 7 - (bitPos % 8);
            if (byteIdx >= 0 && byteIdx < data.size()) {
                if (p[byteIdx] & (1 << bitIdx))
                    raw |= (1ULL << i);
            }
            // Move to next bit in Motorola order
            if (bitPos % 8 == 0)
                bitPos += 15;
            else
                bitPos -= 1;
        }
    }

    // Mask to bit length
    quint64 mask = (bitLength >= 64) ? ~0ULL : ((1ULL << bitLength) - 1);
    raw &= mask;

    // Sign extension
    double value;
    if (isSigned && bitLength < 64) {
        quint64 signBit = 1ULL << (bitLength - 1);
        if (raw & signBit)
            raw |= ~mask; // extend sign
        value = static_cast<double>(static_cast<qint64>(raw));
    } else {
        value = static_cast<double>(raw);
    }

    return value * factor + offset;
}

// ============================================================
//  DbcManager
// ============================================================

DbcManager::DbcManager(QObject *parent)
    : QObject(parent)
{
}

bool DbcManager::loadDbc(const QString &filePath)
{
    DbcFile file;
    if (!parseDbc(filePath, file))
        return false;

    postProcess(file);
    m_files.append(file);
    emit dbcLoaded(file.fileName);
    return true;
}

void DbcManager::unloadDbc(const QString &filePath)
{
    for (int i = 0; i < m_files.size(); ++i) {
        if (m_files[i].filePath == filePath) {
            QString name = m_files[i].fileName;
            m_files.removeAt(i);
            emit dbcUnloaded(name);
            return;
        }
    }
}

const DbcMessage *DbcManager::findMessage(quint32 id) const
{
    for (const auto &f : m_files) {
        const DbcMessage *m = f.findMessage(id);
        if (m) return m;
    }
    return nullptr;
}

const DbcSignal *DbcManager::findSignal(quint32 id, const QString &signalName) const
{
    const DbcMessage *msg = findMessage(id);
    if (!msg) return nullptr;
    return msg->findSignal(signalName);
}

QList<const DbcMessage *> DbcManager::allMessages() const
{
    QList<const DbcMessage *> result;
    for (const auto &f : m_files)
        for (const auto &m : f.messages)
            result.append(&m);
    return result;
}

const DbcFile *DbcManager::findFile(const QString &fileName) const
{
    for (const auto &f : m_files)
        if (f.fileName == fileName) return &f;
    return nullptr;
}

// ============================================================
//  完整 DBC 解析器
//  支持: VERSION, BU_, BO_, SG_, CM_, BA_DEF_, BA_DEF_DEF_,
//        BA_, VAL_, VAL_TABLE_, SIG_VALTYPE_, BO_TX_BU_
// ============================================================

bool DbcManager::parseDbc(const QString &filePath, DbcFile &out)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QFileInfo fi(filePath);
    out.filePath = filePath;
    out.fileName = fi.fileName();

    QTextStream in(&file);
    QStringList lines;
    while (!in.atEnd())
        lines.append(in.readLine());

    // ---- 预处理：合并多行 CM_ 段 ----
    // CM_ 注释可能跨行，以 ; 结尾
    QStringList mergedLines;
    int i = 0;
    while (i < lines.size()) {
        QString line = lines[i].trimmed();
        if (line.startsWith("CM_") && !line.endsWith(';')) {
            // 合并后续行直到遇到 ;
            QString merged = line;
            ++i;
            while (i < lines.size() && !merged.endsWith(';')) {
                merged += "\n" + lines[i];
                ++i;
            }
            mergedLines.append(merged);
        } else {
            if (!line.isEmpty())
                mergedLines.append(line);
            ++i;
        }
    }

    // ---- 逐行解析 ----
    DbcMessage *currentMsg = nullptr;

    // 正则表达式
    QRegularExpression boRe(R"(BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))");
    // SG_ name : startBit|length@endian+sign (factor,offset) [min|max] "unit" receiver
    // 可选多路复用标记: M (multiplexor) 或 m<value> (multiplexed)
    QRegularExpression sgRe(
        R"re(SG_\s+(\w+)\s+(M|m\d+)?\s*:\s*(\d+)\|(\d+)@(\d+)([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^,]+),([^]]+)\]\s*"([^"]*)"\s+(.+))re");
    QRegularExpression buRe(R"(BU_\s*:\s*(.+))");
    QRegularExpression valTableRe(
        R"re(VAL_TABLE_\s+(\w+)\s+(.*);)re");
    QRegularExpression valRe(
        R"re(VAL_\s+(\d+)\s+(\w+)\s+(.*);)re");
    QRegularExpression baDefRe(
        R"re(BA_DEF_\s+(BU_|BO_|SG_)?\s*"([^"]+)"\s+(\w+)\s*(.*);)re");
    QRegularExpression baDefDefRe(
        R"re(BA_DEF_DEF_\s*"([^"]+)"\s+(.*);)re");
    QRegularExpression baRe(
        R"re(BA_\s*"([^"]+)"\s+(BO_|SG_|BU_)?\s*(.*);)re");
    QRegularExpression sigValTypeRe(
        R"re(SIG_VALTYPE_\s+(\d+)\s+(\w+)\s+(\d+))re");
    QRegularExpression boTxBuRe(
        R"re(BO_TX_BU_\s+(\d+)\s*:\s*(.+);)re");

    for (const QString &raw : mergedLines) {
        QString line = raw.trimmed();

        // VERSION
        if (line.startsWith("VERSION")) {
            auto match = QRegularExpression(R"re(VERSION\s+"([^"]*)")re").match(line);
            if (match.hasMatch())
                out.version = match.captured(1);
            continue;
        }

        // NS_ — new symbols, skip
        if (line.startsWith("NS_"))
            continue;

        // BS_ — bit timing, skip
        if (line.startsWith("BS_"))
            continue;

        // BU_ — nodes
        auto buMatch = buRe.match(line);
        if (buMatch.hasMatch()) {
            QStringList nodeNames = buMatch.captured(1).trimmed().split(' ', Qt::SkipEmptyParts);
            for (const auto &n : nodeNames)
                out.nodes.append({n, {}, {}, {}});
            continue;
        }

        // BO_ — message
        auto boMatch = boRe.match(line);
        if (boMatch.hasMatch()) {
            DbcMessage msg;
            msg.id = boMatch.captured(1).toUInt();
            msg.name = boMatch.captured(2);
            msg.dlc = boMatch.captured(3).toInt();
            msg.sender = boMatch.captured(4);
            out.messages.append(msg);
            currentMsg = &out.messages.last();
            continue;
        }

        // SG_ — signal
        auto sgMatch = sgRe.match(line);
        if (sgMatch.hasMatch() && currentMsg) {
            DbcSignal sig;
            sig.name = sgMatch.captured(1);

            // 多路复用标记
            QString muxStr = sgMatch.captured(2);
            if (muxStr == "M") {
                sig.muxType = DbcSignal::MuxType::Multiplexor;
            } else if (muxStr.startsWith('m') && muxStr.size() > 1) {
                sig.muxType = DbcSignal::MuxType::Multiplexed;
                sig.muxValue = muxStr.mid(1).toInt();
            }

            sig.startBit = sgMatch.captured(3).toInt();
            sig.bitLength = sgMatch.captured(4).toInt();
            sig.littleEndian = (sgMatch.captured(5).toInt() == 1);
            sig.isSigned = (sgMatch.captured(6) == QLatin1String("-"));
            sig.factor = sgMatch.captured(7).toDouble();
            sig.offset = sgMatch.captured(8).toDouble();
            sig.minimum = sgMatch.captured(9).toDouble();
            sig.maximum = sgMatch.captured(10).toDouble();
            sig.unit = sgMatch.captured(11);
            sig.receiver = sgMatch.captured(12).trimmed();

            currentMsg->signalList.append(sig);
            continue;
        }

        // CM_ — comments
        if (line.startsWith("CM_")) {
            // CM_ BU_ NodeName "comment";
            // CM_ BO_ id "comment";
            // CM_ SG_ id signalName "comment";
            // CM_ "general comment";
            QRegularExpression cmBuRe(R"re(CM_\s+BU_\s+(\w+)\s+"(.*)"\s*;)re");
            QRegularExpression cmBoRe(R"re(CM_\s+BO_\s+(\d+)\s+"(.*)"\s*;)re");
            QRegularExpression cmSgRe(R"re(CM_\s+SG_\s+(\d+)\s+(\w+)\s+"(.*)"\s*;)re");

            auto cmBu = cmBuRe.match(line);
            if (cmBu.hasMatch()) {
                QString nodeName = cmBu.captured(1);
                QString text = cmBu.captured(2);
                text.replace("\\\"", "\"");
                for (auto &n : out.nodes)
                    if (n.name == nodeName) { n.comment = text; break; }
                continue;
            }

            auto cmBo = cmBoRe.match(line);
            if (cmBo.hasMatch()) {
                quint32 id = cmBo.captured(1).toUInt();
                QString text = cmBo.captured(2);
                text.replace("\\\"", "\"");
                auto *msg = out.findMessage(id);
                if (msg) msg->comment = text;
                continue;
            }

            auto cmSg = cmSgRe.match(line);
            if (cmSg.hasMatch()) {
                quint32 id = cmSg.captured(1).toUInt();
                QString sigName = cmSg.captured(2);
                QString text = cmSg.captured(3);
                text.replace("\\\"", "\"");
                auto *msg = out.findMessage(id);
                if (msg) {
                    auto *sig = msg->findSignal(sigName);
                    if (sig) sig->comment = text;
                }
                continue;
            }
            continue;
        }

        // BA_DEF_ — attribute definition
        auto baDefMatch = baDefRe.match(line);
        if (baDefMatch.hasMatch()) {
            DbcAttributeDef def;
            def.name = baDefMatch.captured(2);
            QString scopeStr = baDefMatch.captured(1);
            QString typeStr = baDefMatch.captured(3);
            QString config = baDefMatch.captured(4).trimmed();

            if (scopeStr == "BU_") def.scope = DbcAttributeDef::Scope::Node;
            else if (scopeStr == "BO_") def.scope = DbcAttributeDef::Scope::Message;
            else if (scopeStr == "SG_") def.scope = DbcAttributeDef::Scope::Signal;
            else def.scope = DbcAttributeDef::Scope::Network;

            if (typeStr == "INT") {
                def.dataType = DbcAttributeDef::DataType::Int;
                QStringList parts = config.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    def.minValue = parts[0].toInt();
                    def.maxValue = parts[1].toInt();
                }
            } else if (typeStr == "FLOAT") {
                def.dataType = DbcAttributeDef::DataType::Float;
                QStringList parts = config.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    def.minValue = parts[0].toDouble();
                    def.maxValue = parts[1].toDouble();
                }
            } else if (typeStr == "STRING") {
                def.dataType = DbcAttributeDef::DataType::String;
            } else if (typeStr == "ENUM") {
                def.dataType = DbcAttributeDef::DataType::Enum;
                // 枚举值用逗号分隔，可能带引号
                QString enumStr = config;
                if (enumStr.startsWith('"') && enumStr.endsWith('"'))
                    enumStr = enumStr.mid(1, enumStr.size() - 2);
                def.enumChoices = enumStr.split(',', Qt::SkipEmptyParts);
                for (auto &s : def.enumChoices)
                    s = s.trimmed();
            }

            out.attributeDefs.append(def);
            continue;
        }

        // BA_DEF_DEF_ — default attribute value
        auto baDefDefMatch = baDefDefRe.match(line);
        if (baDefDefMatch.hasMatch()) {
            QString attrName = baDefDefMatch.captured(1);
            QString valStr = baDefDefMatch.captured(2).trimmed();
            // 去掉引号
            if (valStr.startsWith('"') && valStr.endsWith('"'))
                valStr = valStr.mid(1, valStr.size() - 2);

            for (auto &def : out.attributeDefs) {
                if (def.name == attrName) {
                    if (def.dataType == DbcAttributeDef::DataType::Int)
                        def.defaultValue = valStr.toInt();
                    else if (def.dataType == DbcAttributeDef::DataType::Float)
                        def.defaultValue = valStr.toDouble();
                    else
                        def.defaultValue = valStr;
                    break;
                }
            }
            continue;
        }

        // BA_ — attribute value
        auto baMatch = baRe.match(line);
        if (baMatch.hasMatch()) {
            QString attrName = baMatch.captured(1);
            QString scopeStr = baMatch.captured(2);
            QString rest = baMatch.captured(3).trimmed();

            DbcAttributeValue av;
            av.attributeName = attrName;

            if (scopeStr == "BU_") {
                // BA_ "attr" BU_ NodeName value;
                QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    av.nodeName = parts[0];
                    QString valStr = parts[1];
                    if (valStr.startsWith('"') && valStr.endsWith('"'))
                        valStr = valStr.mid(1, valStr.size() - 2);
                    av.value = valStr;
                }
            } else if (scopeStr == "BO_") {
                // BA_ "attr" BO_ id value;
                QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    av.canId = parts[0].toUInt();
                    QString valStr = parts[1];
                    if (valStr.startsWith('"') && valStr.endsWith('"'))
                        valStr = valStr.mid(1, valStr.size() - 2);
                    bool ok;
                    int intVal = valStr.toInt(&ok);
                    if (ok) av.value = intVal;
                    else av.value = valStr;
                }
            } else if (scopeStr == "SG_") {
                // BA_ "attr" SG_ id signalName value;
                QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    av.canId = parts[0].toUInt();
                    av.signalName = parts[1];
                    QString valStr = parts[2];
                    if (valStr.startsWith('"') && valStr.endsWith('"'))
                        valStr = valStr.mid(1, valStr.size() - 2);
                    bool ok;
                    int intVal = valStr.toInt(&ok);
                    if (ok) av.value = intVal;
                    else av.value = valStr;
                }
            } else {
                // Network-level attribute: BA_ "attr" value;
                QString valStr = rest;
                if (valStr.startsWith('"') && valStr.endsWith('"'))
                    valStr = valStr.mid(1, valStr.size() - 2);
                av.value = valStr;
            }

            out.attributeValues.append(av);
            continue;
        }

        // VAL_TABLE_ — named value table
        auto valTableMatch = valTableRe.match(line);
        if (valTableMatch.hasMatch()) {
            DbcValueTable vt;
            vt.name = valTableMatch.captured(1);
            QString rest = valTableMatch.captured(2).trimmed();

            // 解析 "value "desc" value "desc" ..." 对
            QRegularExpression pairRe(R"re((\d+)\s+"([^"]*)")re");
            auto it = pairRe.globalMatch(rest);
            while (it.hasNext()) {
                auto m = it.next();
                DbcValueDesc vd;
                vd.value = m.captured(1).toInt();
                vd.description = m.captured(2);
                vt.entries.append(vd);
            }
            out.valueTables.append(vt);
            continue;
        }

        // VAL_ — signal value table (inline)
        auto valMatch = valRe.match(line);
        if (valMatch.hasMatch()) {
            quint32 id = valMatch.captured(1).toUInt();
            QString sigName = valMatch.captured(2);
            QString rest = valMatch.captured(3).trimmed();

            auto *msg = out.findMessage(id);
            if (msg) {
                auto *sig = msg->findSignal(sigName);
                if (sig) {
                    QRegularExpression pairRe(R"re((\d+)\s+"([^"]*)")re");
                    auto it = pairRe.globalMatch(rest);
                    while (it.hasNext()) {
                        auto m = it.next();
                        DbcValueDesc vd;
                        vd.value = m.captured(1).toInt();
                        vd.description = m.captured(2);
                        sig->valueTable.append(vd);
                    }
                }
            }
            continue;
        }

        // SIG_VALTYPE_ — signal value type
        auto sigValTypeMatch = sigValTypeRe.match(line);
        if (sigValTypeMatch.hasMatch()) {
            // 1=IEEE float, 2=IEEE double
            // 记录但当前不影响解码逻辑
            continue;
        }

        // BO_TX_BU_ — message transmitter nodes
        auto boTxBuMatch = boTxBuRe.match(line);
        if (boTxBuMatch.hasMatch()) {
            quint32 id = boTxBuMatch.captured(1).toUInt();
            QStringList nodes = boTxBuMatch.captured(2).split(',', Qt::SkipEmptyParts);
            auto *msg = out.findMessage(id);
            if (msg) {
                for (auto &n : nodes)
                    msg->txNodes.append(n.trimmed());
            }
            continue;
        }
    }

    return !out.messages.isEmpty();
}

// ============================================================
//  后处理：关联节点收发关系、应用属性值
// ============================================================

void DbcManager::postProcess(DbcFile &file)
{
    // 1. 关联节点发送的报文
    for (const auto &msg : file.messages) {
        for (auto &node : file.nodes) {
            if (node.name == msg.sender) {
                node.txMessageIds.append(msg.id);
                break;
            }
        }
    }

    // 2. 关联节点接收的信号
    for (const auto &msg : file.messages) {
        for (const auto &sig : msg.signalList) {
            // receiver 可能是逗号分隔的多个节点
            QStringList receivers = sig.receiver.split(',', Qt::SkipEmptyParts);
            for (const auto &r : receivers) {
                QString rName = r.trimmed();
                for (auto &node : file.nodes) {
                    if (node.name == rName) {
                        node.rxSignals.append({msg.id, sig.name});
                        break;
                    }
                }
            }
        }
    }

    // 3. 应用 BA_ 属性到报文（GenMsgCycleTime, GenMsgSendType 等）
    for (auto &msg : file.messages) {
        // GenMsgCycleTime
        QVariant cycleTimeVal = file.getAttributeValue("GenMsgCycleTime", msg.id);
        if (cycleTimeVal.isValid())
            msg.cycleTime = cycleTimeVal.toInt();

        // GenMsgSendType
        QVariant sendTypeVal = file.getAttributeValue("GenMsgSendType", msg.id);
        if (sendTypeVal.isValid())
            msg.sendType = sendTypeVal.toString();
    }
}
