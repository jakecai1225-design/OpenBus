#include "dbcmanager.h"

#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QRegularExpression>
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

// ============================================================
//  简化版 DBC 解析器
//  支持 BO_ (message) 和 SG_ (signal) 关键字
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
    DbcMessage *currentMsg = nullptr;

    QRegularExpression boRe(
        R"(BO_\s+(\d+)\s+(\w+)\s*:\s*(\d+)\s+(\w+))");
    QRegularExpression sgRe(
        R"re( SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@(\d+)([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^,]+),([^]]+)\]\s*"([^"]*)"\s+(\w+))re");

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

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

        auto sgMatch = sgRe.match(line);
        if (sgMatch.hasMatch() && currentMsg) {
            DbcSignal sig;
            sig.name = sgMatch.captured(1);
            sig.startBit = sgMatch.captured(2).toInt();
            sig.bitLength = sgMatch.captured(3).toInt();
            sig.littleEndian = (sgMatch.captured(4).toInt() == 1);
            sig.isSigned = (sgMatch.captured(5) == QLatin1String("-"));
            sig.factor = sgMatch.captured(6).toDouble();
            sig.offset = sgMatch.captured(7).toDouble();
            sig.minimum = sgMatch.captured(8).toDouble();
            sig.maximum = sgMatch.captured(9).toDouble();
            sig.unit = sgMatch.captured(10);
            sig.receiver = sgMatch.captured(11);
            currentMsg->signalList.append(sig);
        }
    }

    return !out.messages.isEmpty();
}
