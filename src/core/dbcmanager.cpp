#include "dbcmanager.h"

#include <cmath>
#include <cstring>

#include "dbc/dbc_adapter.h"

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
    if (!dbc::parse(filePath, file))
        return false;

    dbc::postProcess(file);
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
