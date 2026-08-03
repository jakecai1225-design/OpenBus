#include "trc_reader.h"
#include "core/canframe.h"

#include <QTextStream>
#include <QStringList>
#include <QRegularExpression>

// ============================================================
//  TrcReader
// ============================================================

TrcReader::TrcReader() = default;

TrcReader::~TrcReader()
{
    close();
}

bool TrcReader::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    return m_file.open(QIODevice::ReadOnly | QIODevice::Text);
}

int TrcReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    QTextStream ts(&m_file);
    ts.setEncoding(QStringConverter::Utf8);

    double baseTime = 0.0;
    bool hasBaseTime = false;
    int count = 0;
    int seq = 0;
    bool inHeader = false;

    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty())
            continue;

        // TRC v2.0 header markers
        if (line.startsWith("$HEADER", Qt::CaseInsensitive)) {
            inHeader = true;
            continue;
        }
        if (line.startsWith("$END", Qt::CaseInsensitive)) {
            inHeader = false;
            continue;
        }
        if (inHeader)
            continue;

        // 跳过注释行
        if (line.startsWith(';') || line.startsWith('#') || line.startsWith("//"))
            continue;

        // 跳过列标题行（通常包含 "Nr." 或 "Time" 等关键字）
        if (line.contains("Nr.", Qt::CaseInsensitive) ||
            line.contains("Time", Qt::CaseInsensitive) ||
            line.contains("---", Qt::CaseInsensitive))
            continue;

        // 尝试解析帧行
        CanFrame frame;
        if (parseLine(line, frame, ++seq)) {
            if (!hasBaseTime) {
                baseTime = frame.timestamp;
                hasBaseTime = true;
            }
            frame.timestamp -= baseTime;
            frames.append(frame);
            ++count;
        }
    }

    return count;
}

bool TrcReader::parseLine(const QString &line, CanFrame &frame, int seq) const
{
    // TRC 格式有多种变体，这里处理最常见的格式：
    //
    // v1.0: seq time channel id direction dlc data...
    //       1 0.000123 1 123 Rx 8 01 02 03 04 05 06 07 08
    //
    // v2.0: seq time ch id type dlc data... (类型字段可能不同)
    //       1 0.000123 1 123 Rx 8 01 02 03 04 05 06 07 08

    const auto tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (tokens.size() < 6)
        return false;

    bool ok = false;
    int idx = 0;

    // 序号（可能不存在，跳过验证）
    int seqNum = tokens[idx].toInt(&ok);
    if (ok) {
        ++idx; // 跳过序号
    } else {
        // 如果第一个 token 不是数字，可能没有序号
        seqNum = seq;
    }

    if (idx >= tokens.size()) return false;

    // 时间戳
    frame.timestamp = tokens[idx].toDouble(&ok);
    if (!ok) return false;
    ++idx;

    if (idx >= tokens.size()) return false;

    // 通道
    frame.channel = static_cast<quint8>(tokens[idx].toUInt(&ok));
    if (!ok) frame.channel = 1;
    ++idx;

    if (idx >= tokens.size()) return false;

    // CAN ID
    QString idStr = tokens[idx];
    if (idStr.toLower().endsWith("x")) {
        frame.extended = true;
        idStr.chop(1);
    }
    frame.id = idStr.toUInt(&ok, 16);
    if (!ok) {
        // 尝试十进制
        frame.id = idStr.toUInt(&ok);
        if (!ok) return false;
    }
    ++idx;

    if (idx >= tokens.size()) return false;

    // 方向 / 帧类型
    QString dirStr = tokens[idx].toUpper();
    if (dirStr == "TX") {
        frame.direction = CanFrame::Tx;
        ++idx;
    } else if (dirStr == "RX") {
        frame.direction = CanFrame::Rx;
        ++idx;
    } else if (dirStr == "FD" || dirStr.startsWith("FD")) {
        // 可能是 FD 标记而非方向
        frame.fd = true;
        if (dirStr.contains("X", Qt::CaseInsensitive))
            frame.bitrateSwitch = true;
        ++idx;
        if (idx >= tokens.size()) return false;
        // 接下来应该是方向
        dirStr = tokens[idx].toUpper();
        if (dirStr == "TX") {
            frame.direction = CanFrame::Tx;
            ++idx;
        } else if (dirStr == "RX") {
            frame.direction = CanFrame::Rx;
            ++idx;
        }
    } else {
        frame.direction = CanFrame::Rx;
    }

    if (idx >= tokens.size()) return false;

    // DLC
    int dlc = tokens[idx].toUInt(&ok);
    if (!ok) return false;
    if (frame.fd) {
        frame.dlc = CanFrame::lengthToDlc(dlc);
    } else {
        frame.dlc = static_cast<quint8>(qMin(dlc, 8));
    }
    ++idx;

    // 数据字节
    int dataLen = frame.fd ? CanFrame::dlcToLength(frame.dlc) : frame.dlc;
    dataLen = qMin(dataLen, 64);
    int available = tokens.size() - idx;
    dataLen = qMin(dataLen, available);
    frame.data.resize(dataLen);

    for (int i = 0; i < dataLen; ++i) {
        bool hexOk = false;
        unsigned int val = tokens[idx + i].toUInt(&hexOk, 16);
        if (hexOk && val <= 0xFF)
            frame.data[i] = static_cast<char>(val);
        else
            frame.data.resize(i);
    }

    return true;
}

void TrcReader::close()
{
    m_file.close();
}
