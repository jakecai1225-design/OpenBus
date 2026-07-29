#include "dbcmanager.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QStringConverter>
#include <QDebug>
#include <cmath>

// ============================================================
//  DbcSignal::rawDecode / rawToPhys / decode
//  位遍历算法参考 dbcppp (SignalImpl.cpp) 的实现
//  Motorola: 从 startBit 开始逐位遍历，字节内向前移动
//  Intel: 从 startBit 开始逐位遍历，连续递增
// ============================================================

quint64 DbcSignal::rawDecode(const QByteArray &data) const
{
    if (data.isEmpty() || bitLength <= 0)
        return 0;

    const unsigned char *p = reinterpret_cast<const unsigned char *>(data.constData());
    int dataSize = data.size();
    quint64 raw = 0;

    if (littleEndian) {
        // Intel byte order: bitPos 连续递增
        for (int i = 0; i < bitLength; ++i) {
            int bitPos = startBit + i;
            int byteIdx = bitPos / 8;
            int bitIdx = bitPos % 8;
            if (byteIdx >= 0 && byteIdx < dataSize) {
                if (p[byteIdx] & (1u << bitIdx))
                    raw |= (1ULL << i);
            }
        }
    } else {
        // Motorola byte order (big endian)
        // 参考 dbcppp: 从 startBit 开始，在字节内向前移动
        // 当到达字节起始(src%8==0)时跳到下一个高字节的 bit7
        int src = startBit;
        for (int i = 0; i < bitLength; ++i) {
            int byteIdx = src / 8;
            int bitIdx = src % 8;
            if (byteIdx >= 0 && byteIdx < dataSize) {
                if (p[byteIdx] & (1u << bitIdx))
                    raw |= (1ULL << (bitLength - 1 - i));
            }
            if ((src % 8) == 0)
                src += 15;
            else
                src -= 1;
        }
    }

    return raw;
}

double DbcSignal::rawToPhys(quint64 raw) const
{
    double phys;

    if (extendedValueType == ExtendedValueType::Float && bitLength == 32) {
        // IEEE 754 float
        float f;
        quint32 val = static_cast<quint32>(raw);
        memcpy(&f, &val, sizeof(f));
        phys = static_cast<double>(f);
    } else if (extendedValueType == ExtendedValueType::Double && bitLength == 64) {
        // IEEE 754 double
        memcpy(&phys, &raw, sizeof(double));
    } else {
        // Integer
        quint64 mask = (bitLength >= 64) ? ~0ULL : ((1ULL << bitLength) - 1);
        raw &= mask;

        if (isSigned && bitLength < 64) {
            quint64 signBit = 1ULL << (bitLength - 1);
            if (raw & signBit)
                raw |= ~mask; // extend sign
            phys = static_cast<double>(static_cast<qint64>(raw));
        } else {
            phys = static_cast<double>(raw);
        }
    }

    return phys * factor + offset;
}

double DbcSignal::decode(const QByteArray &data) const
{
    return rawToPhys(rawDecode(data));
}

// ============================================================
//  DbcSignal::encode — rawDecode 的逆操作
//  将物理值转换为 raw 后写入 data 的对应位
// ============================================================
void DbcSignal::encode(QByteArray &data, double physValue) const
{
    if (bitLength <= 0)
        return;

    // 物理值 -> raw 值 (rawToPhys 的逆)
    quint64 raw = 0;
    if (extendedValueType == ExtendedValueType::Float && bitLength == 32) {
        float f = static_cast<float>(physValue);
        quint32 val;
        memcpy(&val, &f, sizeof(f));
        raw = val;
    } else if (extendedValueType == ExtendedValueType::Double && bitLength == 64) {
        memcpy(&raw, &physValue, sizeof(double));
    } else {
        double rawVal = (factor != 0.0) ? (physValue - offset) / factor : 0.0;
        raw = static_cast<quint64>(std::llround(rawVal));
    }

    // 确保 data 足够长
    int neededBytes = 0;
    if (littleEndian) {
        neededBytes = (startBit + bitLength + 7) / 8;
    } else {
        // Motorola: 遍历所有位求最大 byteIdx
        int src = startBit;
        for (int i = 0; i < bitLength; ++i) {
            int byteIdx = src / 8;
            if (byteIdx + 1 > neededBytes) neededBytes = byteIdx + 1;
            if ((src % 8) == 0) src += 15; else src -= 1;
        }
    }
    while (data.size() < neededBytes)
        data.append(static_cast<char>(0));

    // 写入位 (rawDecode 的逆操作)
    char *p = data.data();
    int dataSize = data.size();

    if (littleEndian) {
        for (int i = 0; i < bitLength; ++i) {
            int bitPos = startBit + i;
            int byteIdx = bitPos / 8;
            int bitIdx = bitPos % 8;
            if (byteIdx >= 0 && byteIdx < dataSize) {
                if (raw & (1ULL << i))
                    p[byteIdx] |= static_cast<char>(1u << bitIdx);
                else
                    p[byteIdx] &= static_cast<char>(~(1u << bitIdx));
            }
        }
    } else {
        int src = startBit;
        for (int i = 0; i < bitLength; ++i) {
            int byteIdx = src / 8;
            int bitIdx = src % 8;
            if (byteIdx >= 0 && byteIdx < dataSize) {
                if (raw & (1ULL << (bitLength - 1 - i)))
                    p[byteIdx] |= static_cast<char>(1u << bitIdx);
                else
                    p[byteIdx] &= static_cast<char>(~(1u << bitIdx));
            }
            if ((src % 8) == 0) src += 15; else src -= 1;
        }
    }
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
    rebuildIndex();
    emit dbcLoaded(file.fileName);
    return true;
}

void DbcManager::unloadDbc(const QString &filePath)
{
    for (int i = 0; i < m_files.size(); ++i) {
        if (m_files[i].filePath == filePath) {
            QString name = m_files[i].fileName;
            m_files.removeAt(i);
            rebuildIndex();
            emit dbcUnloaded(name);
            return;
        }
    }
}

void DbcManager::rebuildIndex()
{
    m_msgIndex.clear();
    for (const auto &f : m_files)
        for (const auto &m : f.messages)
            m_msgIndex.insert(m.id, &m);
}

const DbcMessage *DbcManager::findMessage(quint32 id) const
{
    auto it = m_msgIndex.constFind(id);
    return (it != m_msgIndex.constEnd()) ? it.value() : nullptr;
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
//  批量信号解码 — 供 Trace / Graphic 实时调用
// ============================================================

QVector<DbcManager::DecodedSignal> DbcManager::decodeFrame(quint32 canId, const QByteArray &data) const
{
    QVector<DecodedSignal> result;
    const DbcMessage *msg = findMessage(canId);
    if (!msg)
        return result;

    result.reserve(msg->signalList.size());
    for (const auto &sig : msg->signalList) {
        DecodedSignal ds;
        ds.name = sig.name;
        ds.physValue = sig.decode(data);
        ds.unit = sig.unit;
        ds.comment = sig.comment;
        // 值表描述
        int rawInt = static_cast<int>(sig.rawDecode(data));
        ds.valueDesc = sig.lookupValueDesc(rawInt);
        result.append(ds);
    }
    return result;
}

bool DbcManager::decodeSignal(quint32 canId, const QString &sigName,
                              const QByteArray &data, double &outValue) const
{
    const DbcSignal *sig = findSignal(canId, sigName);
    if (!sig)
        return false;
    outValue = sig->decode(data);
    return true;
}

// ============================================================
//  DBC 解析器
//  参考 dbcppp (github.com/xR3b0rn/dbcppp) 的 DBC 语法规则
//  支持: VERSION, NS_, BS_, BU_, BO_, SG_, CM_, BA_DEF_, BA_DEF_DEF_,
//        BA_, VAL_, VAL_TABLE_, SIG_VALTYPE_, BO_TX_BU_
//  预处理: 去除 // 和 /* */ 注释 (参考 dbcppp DBCSkipper)
// ============================================================

/// 去除 DBC 文件中的 // 单行注释和 /* */ 块注释
/// 参考dbcppp DBCSkipper: 同时处理引号内的注释标记不剥离
static QString stripDbcComments(const QString &content)
{
    QString result;
    result.reserve(content.size());
    const int len = content.size();
    int i = 0;
    bool inString = false;

    while (i < len) {
        QChar c = content[i];

        if (inString) {
            result += c;
            if (c == '\\' && i + 1 < len) {
                result += content[i + 1];
                i += 2;
                continue;
            }
            if (c == '"')
                inString = false;
            ++i;
            continue;
        }

        if (c == '"') {
            inString = true;
            result += c;
            ++i;
            continue;
        }

        // // 单行注释
        if (c == '/' && i + 1 < len && content[i + 1] == '/') {
            while (i < len && content[i] != '\n')
                ++i;
            continue;
        }

        // /* 块注释
        if (c == '/' && i + 1 < len && content[i + 1] == '*') {
            i += 2;
            while (i + 1 < len) {
                if (content[i] == '*' && content[i + 1] == '/') {
                    i += 2;
                    break;
                }
                ++i;
            }
            if (i + 1 >= len)
                i = len;  // 防止越界
            continue;
        }

        result += c;
        ++i;
    }
    return result;
}

/// 逐字段解析 SG_ 行，比正则更健壮
/// 格式: SG_ Name [mux] : startBit|bitLen@endianSign (factor,offset) [min|max] "unit" receivers
static bool parseSignalLine(const QString &line, DbcSignal &sig)
{
    QString s = line.trimmed();
    if (!s.startsWith("SG_"))
        return false;
    s = s.mid(3).trimmed();

    // 用 ':' 分割 name+mux 和剩余部分
    int colonPos = s.indexOf(':');
    if (colonPos < 0)
        return false;

    QString nameAndMux = s.left(colonPos).trimmed();
    QString rest = s.mid(colonPos + 1).trimmed();

    // 解析 name 和可选的 mux 标记
    QStringList nameParts = nameAndMux.split(' ', Qt::SkipEmptyParts);
    if (nameParts.isEmpty())
        return false;
    sig.name = nameParts[0];

    if (nameParts.size() > 1) {
        QString muxStr = nameParts[1];
        if (muxStr == "M") {
            sig.muxType = DbcSignal::MuxType::Multiplexor;
        } else if (muxStr.startsWith('m')) {
            sig.muxType = DbcSignal::MuxType::Multiplexed;
            sig.muxValue = muxStr.mid(1).toInt();
        }
    }

    // 用 '|' 分割 startBit 和 bitLen@endianSign
    int pipePos = rest.indexOf('|');
    if (pipePos < 0)
        return false;
    sig.startBit = rest.left(pipePos).trimmed().toInt();

    // 用 '@' 分割 bitLen 和 endianSign
    int atPos = rest.indexOf('@', pipePos);
    if (atPos < 0)
        return false;
    sig.bitLength = rest.mid(pipePos + 1, atPos - pipePos - 1).trimmed().toInt();

    // @ 后第一个字符是 endian (0=Motorola, 1=Intel)，第二个是 sign (+/-)
    if (atPos + 2 >= rest.size())
        return false;
    sig.littleEndian = (rest[atPos + 1] == '1');
    sig.isSigned = (rest[atPos + 2] == '-');

    // 提取 (factor,offset)
    int parenOpen = rest.indexOf('(', atPos);
    int parenClose = rest.indexOf(')', parenOpen);
    if (parenOpen < 0 || parenClose < 0)
        return false;
    QStringList foParts = rest.mid(parenOpen + 1, parenClose - parenOpen - 1).split(',');
    if (foParts.size() >= 2) {
        sig.factor = foParts[0].trimmed().toDouble();
        sig.offset = foParts[1].trimmed().toDouble();
    }

    // 提取 [min|max]
    int bracketOpen = rest.indexOf('[', parenClose);
    int bracketClose = rest.indexOf(']', bracketOpen);
    if (bracketOpen >= 0 && bracketClose >= 0) {
        QStringList mmParts = rest.mid(bracketOpen + 1, bracketClose - bracketOpen - 1).split('|');
        if (mmParts.size() >= 2) {
            sig.minimum = mmParts[0].trimmed().toDouble();
            sig.maximum = mmParts[1].trimmed().toDouble();
        }
    }

    // 提取 "unit"
    int quoteStart = rest.indexOf('"', bracketClose >= 0 ? bracketClose : parenClose);
    int quoteEnd = (quoteStart >= 0) ? rest.indexOf('"', quoteStart + 1) : -1;
    if (quoteStart >= 0 && quoteEnd >= 0)
        sig.unit = rest.mid(quoteStart + 1, quoteEnd - quoteStart - 1);

    // 剩余部分是 receivers
    if (quoteEnd >= 0 && quoteEnd + 1 < rest.size())
        sig.receiver = rest.mid(quoteEnd + 1).trimmed();

    return true;
}

bool DbcManager::parseDbc(const QString &filePath, DbcFile &out)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QFileInfo fi(filePath);
    out.filePath = filePath;
    out.fileName = fi.fileName();

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Latin1);
    QString content = in.readAll();

    // 预处理: 去除注释
    content = stripDbcComments(content);

    // 预处理: 合并多行段 (CM_, BA_, VAL_ 等以 ; 结尾的段)
    QStringList rawLines = content.split('\n');
    QStringList mergedLines;
    int i = 0;
    while (i < rawLines.size()) {
        QString line = rawLines[i].trimmed();
        if (line.isEmpty()) {
            ++i;
            continue;
        }

        // 检查是否需要合并: 引号数量为奇数或不以 ; 结尾
        // 仅对可能跨行的段 (CM_, BA_, VAL_, VAL_TABLE_, BA_DEF_)
        // 注意: NS_ 段中也有 CM_/BA_/VAL_/BA_DEF_ 等关键字名，必须排除
        //       判断依据：真正的段行在关键字后有空格/Tab/引号，而 NS_ 关键字名是裸关键字
        auto isSectionStart = [](const QString &s, const char *prefix) -> bool {
            int len = int(strlen(prefix));
            return s.startsWith(prefix) && s.size() > len &&
                   (s[len] == ' ' || s[len] == '\t' || s[len] == '"');
        };
        bool needsMerge = false;
        if (isSectionStart(line, "CM_") || isSectionStart(line, "BA_DEF_") ||
            isSectionStart(line, "VAL_") || isSectionStart(line, "BA_")) {
            // 引号为奇数说明跨行
            int quoteCount = line.count('"');
            if (quoteCount % 2 != 0 || !line.endsWith(';'))
                needsMerge = true;
        }

        if (needsMerge) {
            QString merged = line;
            ++i;
            while (i < rawLines.size()) {
                merged += "\n" + rawLines[i];
                QString mergedTrimmed = merged.trimmed();
                int qCount = mergedTrimmed.count('"');
                if (qCount % 2 == 0 && mergedTrimmed.endsWith(';'))
                    break;
                ++i;
            }
            mergedLines.append(merged.trimmed());
        } else {
            mergedLines.append(line);
            ++i;
        }
    }

    qDebug() << "[DBC] Parsing:" << out.fileName
             << "lines:" << mergedLines.size();

    // 保留的正则表达式 (非 SG_ 的段)
    QRegularExpression boRe(R"(BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))");
    QRegularExpression buRe(R"(BU_\s*:\s*(.+))");
    QRegularExpression valTableRe(R"re(VAL_TABLE_\s+(\w+)\s+(.*);)re");
    QRegularExpression valRe(R"re(VAL_\s+(\d+)\s+(\w+)\s+(.*);)re");
    QRegularExpression baDefRe(R"re(BA_DEF_\s+(BU_|BO_|SG_)?\s*"([^"]+)"\s+(\w+)\s*(.*);)re");
    QRegularExpression baDefDefRe(R"re(BA_DEF_DEF_\s*"([^"]+)"\s+(.*);)re");
    QRegularExpression baRe(R"re(BA_\s*"([^"]+)"\s+(BO_|SG_|BU_)?\s*(.*);)re");
    QRegularExpression sigValTypeRe(R"re(SIG_VALTYPE_\s+(\d+)\s+(\w+)\s+(\d+))re");
    QRegularExpression boTxBuRe(R"re(BO_TX_BU_\s+(\d+)\s*:\s*(.+);)re");
    QRegularExpression pairRe(R"re((\d+)\s+"([^"]*)")re");

    DbcMessage *currentMsg = nullptr;

    for (const QString &raw : mergedLines) {
        QString line = raw.trimmed();

        if (line.startsWith("VERSION")) {
            auto m = QRegularExpression(R"re(VERSION\s+"([^"]*)")re").match(line);
            if (m.hasMatch())
                out.version = m.captured(1);
            continue;
        }
        if (line.startsWith("NS_") || line.startsWith("BS_"))
            continue;

        // BU_ — nodes
        if (line.startsWith("BU_")) {
            auto m = buRe.match(line);
            if (m.hasMatch()) {
                for (const auto &n : m.captured(1).trimmed().split(' ', Qt::SkipEmptyParts))
                    out.nodes.append({n, {}, {}, {}});
            }
            continue;
        }

        // BO_ — message
        if (line.startsWith("BO_")) {
            auto m = boRe.match(line);
            if (m.hasMatch()) {
                DbcMessage msg;
                msg.id = m.captured(1).toUInt();
                msg.name = m.captured(2);
                msg.dlc = m.captured(3).toInt();
                msg.sender = m.captured(4);
                out.messages.append(msg);
                currentMsg = &out.messages.last();
            }
            continue;
        }

        // SG_ — signal (逐字段解析)
        if (line.startsWith("SG_") && currentMsg) {
            DbcSignal sig;
            if (parseSignalLine(line, sig))
                currentMsg->signalList.append(sig);
            continue;
        }

        // CM_ — comments
        if (line.startsWith("CM_")) {
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
                auto *msg = out.findMessage(cmBo.captured(1).toUInt());
                if (msg) {
                    QString text = cmBo.captured(2);
                    text.replace("\\\"", "\"");
                    msg->comment = text;
                }
                continue;
            }
            auto cmSg = cmSgRe.match(line);
            if (cmSg.hasMatch()) {
                auto *msg = out.findMessage(cmSg.captured(1).toUInt());
                if (msg) {
                    auto *sig = msg->findSignal(cmSg.captured(2));
                    if (sig) {
                        QString text = cmSg.captured(3);
                        text.replace("\\\"", "\"");
                        sig->comment = text;
                    }
                }
                continue;
            }
            continue;
        }

        // BA_DEF_ — attribute definition
        if (line.startsWith("BA_DEF_")) {
            auto m = baDefRe.match(line);
            if (m.hasMatch()) {
                DbcAttributeDef def;
                def.name = m.captured(2);
                QString scopeStr = m.captured(1);
                QString typeStr = m.captured(3);
                QString config = m.captured(4).trimmed();

                if (scopeStr == "BU_") def.scope = DbcAttributeDef::Scope::Node;
                else if (scopeStr == "BO_") def.scope = DbcAttributeDef::Scope::Message;
                else if (scopeStr == "SG_") def.scope = DbcAttributeDef::Scope::Signal;
                else def.scope = DbcAttributeDef::Scope::Network;

                if (typeStr == "INT" || typeStr == "HEX") {
                    def.dataType = DbcAttributeDef::DataType::Int;
                    auto parts = config.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 2) { def.minValue = parts[0].toInt(); def.maxValue = parts[1].toInt(); }
                } else if (typeStr == "FLOAT") {
                    def.dataType = DbcAttributeDef::DataType::Float;
                    auto parts = config.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 2) { def.minValue = parts[0].toDouble(); def.maxValue = parts[1].toDouble(); }
                } else if (typeStr == "STRING") {
                    def.dataType = DbcAttributeDef::DataType::String;
                } else if (typeStr == "ENUM") {
                    def.dataType = DbcAttributeDef::DataType::Enum;
                    QString enumStr = config;
                    if (enumStr.startsWith('"') && enumStr.endsWith('"'))
                        enumStr = enumStr.mid(1, enumStr.size() - 2);
                    def.enumChoices = enumStr.split(',', Qt::SkipEmptyParts);
                    for (auto &s : def.enumChoices) s = s.trimmed();
                }
                out.attributeDefs.append(def);
            }
            continue;
        }

        // BA_DEF_DEF_ — default value
        if (line.startsWith("BA_DEF_DEF_")) {
            auto m = baDefDefRe.match(line);
            if (m.hasMatch()) {
                QString attrName = m.captured(1);
                QString valStr = m.captured(2).trimmed();
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
            }
            continue;
        }

        // BA_ — attribute value
        if (line.startsWith("BA_")) {
            auto m = baRe.match(line);
            if (m.hasMatch()) {
                QString attrName = m.captured(1);
                QString scopeStr = m.captured(2);
                QString rest = m.captured(3).trimmed();
                DbcAttributeValue av;
                av.attributeName = attrName;

                if (scopeStr == "BU_") {
                    auto parts = rest.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 2) {
                        av.nodeName = parts[0];
                        QString v = parts[1];
                        if (v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
                        av.value = v;
                    }
                } else if (scopeStr == "BO_") {
                    auto parts = rest.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 2) {
                        av.canId = parts[0].toUInt();
                        QString v = parts[1];
                        if (v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
                        bool ok; int iv = v.toInt(&ok);
                        av.value = ok ? QVariant(iv) : QVariant(v);
                    }
                } else if (scopeStr == "SG_") {
                    auto parts = rest.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 3) {
                        av.canId = parts[0].toUInt();
                        av.signalName = parts[1];
                        QString v = parts[2];
                        if (v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
                        bool ok; int iv = v.toInt(&ok);
                        av.value = ok ? QVariant(iv) : QVariant(v);
                    }
                } else {
                    QString v = rest;
                    if (v.startsWith('"') && v.endsWith('"')) v = v.mid(1, v.size() - 2);
                    av.value = v;
                }
                out.attributeValues.append(av);
            }
            continue;
        }

        // VAL_TABLE_ — named value table
        if (line.startsWith("VAL_TABLE_")) {
            auto m = valTableRe.match(line);
            if (m.hasMatch()) {
                DbcValueTable vt;
                vt.name = m.captured(1);
                auto it = pairRe.globalMatch(m.captured(2).trimmed());
                while (it.hasNext()) {
                    auto mm = it.next();
                    vt.entries.append({mm.captured(1).toInt(), mm.captured(2)});
                }
                out.valueTables.append(vt);
            }
            continue;
        }

        // VAL_ — signal value table (inline)
        if (line.startsWith("VAL_")) {
            auto m = valRe.match(line);
            if (m.hasMatch()) {
                auto *msg = out.findMessage(m.captured(1).toUInt());
                if (msg) {
                    auto *sig = msg->findSignal(m.captured(2));
                    if (sig) {
                        auto it = pairRe.globalMatch(m.captured(3).trimmed());
                        while (it.hasNext()) {
                            auto mm = it.next();
                            sig->valueTable.append({mm.captured(1).toInt(), mm.captured(2)});
                        }
                    }
                }
            }
            continue;
        }

        // SIG_VALTYPE_ — signal value type (1=float, 2=double)
        if (line.startsWith("SIG_VALTYPE_")) {
            auto m = sigValTypeRe.match(line);
            if (m.hasMatch()) {
                quint32 id = m.captured(1).toUInt();
                QString sigName = m.captured(2);
                int valType = m.captured(3).toInt();
                auto *msg = out.findMessage(id);
                if (msg) {
                    auto *sig = msg->findSignal(sigName);
                    if (sig) {
                        if (valType == 1)
                            sig->extendedValueType = DbcSignal::ExtendedValueType::Float;
                        else if (valType == 2)
                            sig->extendedValueType = DbcSignal::ExtendedValueType::Double;
                    }
                }
            }
            continue;
        }

        // BO_TX_BU_ — message transmitter nodes
        if (line.startsWith("BO_TX_BU_")) {
            auto m = boTxBuRe.match(line);
            if (m.hasMatch()) {
                auto *msg = out.findMessage(m.captured(1).toUInt());
                if (msg) {
                    for (auto &n : m.captured(2).split(',', Qt::SkipEmptyParts))
                        msg->txNodes.append(n.trimmed());
                }
            }
            continue;
        }
    }

    qDebug() << "[DBC] Parsed:" << out.messages.size() << "messages,"
             << out.nodes.size() << "nodes,"
             << out.valueTables.size() << "valueTables,"
             << out.attributeDefs.size() << "attrDefs,"
             << out.attributeValues.size() << "attrValues";

    return true;
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
