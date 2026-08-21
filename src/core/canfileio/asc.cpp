#include "asc.h"
#include "core/canframe.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>

// ============================================================
//  AscWriter
// ============================================================

AscWriter::AscWriter() = default;

AscWriter::~AscWriter()
{
    close();
}

bool AscWriter::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);
    m_frameCount = 0;
    m_hasBaseTime = false;

    writeHeader();
    return true;
}

void AscWriter::writeHeader()
{
    // ASC 文件头
    m_stream << "date "
             << QDateTime::currentDateTime().toString("ddd MMM d HH:mm:ss yyyy")
             << "\n";
    m_stream << "base hex  timestamps absolute\n";
    m_stream << "no internal events logged\n";
}

void AscWriter::writeFrame(const CanFrame &frame)
{
    if (!isOpen())
        return;

    if (!m_hasBaseTime) {
        m_baseTime = frame.timestamp;
        m_hasBaseTime = true;
    }

    // 相对时间戳（秒）
    double relTime = frame.timestamp - m_baseTime;
    m_stream << QString::number(relTime, 'f', 6) << " ";

    // 通道号
    m_stream << QString::number(frame.channel) << " ";

    // 方向
    m_stream << (frame.direction == CanFrame::Tx ? "Tx" : "Rx") << " ";

    // 帧类型前缀
    if (frame.fd) {
        m_stream << "FD";
        if (frame.bitrateSwitch) m_stream << "x";
        m_stream << " ";
    }

    // CAN ID（CANoe ASC 规范：扩展帧 ID 后缀 x，如 1ABCDEFx；与 AscReader 的
    // 后缀判定闭环，且与 CANoe 导出的 ASC 文件互换）
    QString idStr = QString("%1")
        .arg(frame.id & (frame.extended ? 0x1FFFFFFF : 0x7FF),
             frame.extended ? 8 : 3, 16, QChar('0')).toUpper();
    if (frame.extended)
        m_stream << idStr << "x";
    else
        m_stream << idStr;

    m_stream << " ";

    // DLC
    if (frame.fd) {
        int actualLen = CanFrame::dlcToLength(frame.dlc);
        m_stream << QString::number(actualLen);
    } else {
        m_stream << QString::number(frame.dlc);
    }
    m_stream << " ";

    // 数据
    if (frame.fd) {
        m_stream << "d "; // data indicator for FD
    }

    for (int i = 0; i < frame.data.size(); ++i) {
        m_stream << QString("%1").arg(static_cast<unsigned char>(frame.data[i]), 2, 16, QChar('0')).toUpper();
        if (i < frame.data.size() - 1)
            m_stream << " ";
    }

    // FD 属性
    if (frame.fd) {
        m_stream << "  ";
        // BRS ESI flags
        if (frame.bitrateSwitch) m_stream << "BRS ";
        if (frame.errorState) m_stream << "ESI";
    }

    m_stream << "\n";

    // 定期刷新缓冲区
    if (++m_frameCount % 1000 == 0)
        m_stream.flush();
}

void AscWriter::close()
{
    if (!isOpen())
        return;

    m_stream.flush();
    m_file.close();
}

// ============================================================
//  AscReader
// ============================================================

AscReader::AscReader() = default;

AscReader::~AscReader()
{
    close();
}

bool AscReader::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    return m_file.open(QIODevice::ReadOnly | QIODevice::Text);
}

// ============================================================
//  AscReader — 行解析辅助
//  解析逻辑与 file_import/AscImporter::parseLine 同源：支持本软件
//  AscWriter 格式（格式A：<ch> <Dir> [FD[x]] <id> ...）与 CANoe
//  7.x/15.x 导出变体（CAN/CANFD 关键字、ID 前置格式B、flags 列、
//  dlc 码/数据长度分离、行尾附加列）。后续统一为共享实现。
// ============================================================

namespace {

/// 判断 token 是否为方向标记
bool isDirToken(const QString &s)
{
    return s.compare(QLatin1String("Rx"), Qt::CaseInsensitive) == 0 ||
           s.compare(QLatin1String("Tx"), Qt::CaseInsensitive) == 0;
}

/// 从 token 解析 CAN ID（支持 0x 前缀和 x 后缀）
bool parseCanId(QString token, quint32 &id, bool &extended)
{
    extended = false;
    if (token.endsWith(QLatin1Char('x'), Qt::CaseInsensitive)) {
        extended = true;
        token.chop(1);
    }
    if (token.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        token = token.mid(2);
    bool ok = false;
    id = token.toUInt(&ok, 16);
    return ok;
}

/// 判断 token 是否为有效数据字节（1~2 位十六进制）
bool isDataByte(const QString &t)
{
    if (t.length() < 1 || t.length() > 2)
        return false;
    bool ok = false;
    t.toUInt(&ok, 16);
    return ok;
}

/// 解析一行报文，失败返回 false（该行跳过）
bool parseFrameLine(const QStringList &tokens, CanFrame &frame)
{
    if (tokens.size() < 5)
        return false;

    // 时间戳
    bool ok = false;
    frame.timestamp = tokens[0].toDouble(&ok);
    if (!ok) return false;

    int idx = 1;

    // 可选 CAN/CANFD 关键字（CANoe 导出）
    bool hasCanKw = false;
    bool isFdKw = false;
    if (tokens[idx].compare(QLatin1String("CAN"), Qt::CaseInsensitive) == 0) {
        hasCanKw = true;
        idx++;
    } else if (tokens[idx].compare(QLatin1String("CANFD"), Qt::CaseInsensitive) == 0) {
        hasCanKw = true;
        isFdKw = true;
        idx++;
    }

    if (idx >= tokens.size()) return false;

    // 通道
    frame.channel = static_cast<quint8>(tokens[idx].toUInt());
    idx++;

    if (idx >= tokens.size()) return false;

    // CANFD 关键字分支的公共尾部：flags×2 [d|D] dlc码 dataLen data...
    // dlc 码字段存在 7.0(十进制)/15.7(十六进制) 变体，不依赖其值 —
    // 按实际读到的数据长度反推 DLC，行尾附加列自然截断
    auto parseCanFdKwTail = [&]() {
        idx += 2; // flags1, flags2
        if (idx < tokens.size() &&
            (tokens[idx] == QLatin1String("d") || tokens[idx] == QLatin1String("D")))
            idx++; // 可选 data 标记
        idx++; // dlc 码（忽略，见上）
        if (idx >= tokens.size()) return false;
        const int dataLen = qMin<int>(tokens[idx].toUInt(), 64);
        idx++;
        frame.fd = true;
        frame.data.clear();
        for (int i = 0; i < dataLen && idx < tokens.size(); ++i) {
            bool ok2 = false;
            quint8 b = static_cast<quint8>(tokens[idx].toUInt(&ok2, 16));
            if (ok2) frame.data.append(static_cast<char>(b));
            idx++;
        }
        frame.dlc = CanFrame::lengthToDlc(frame.data.size());
        return true;
    };

    if (isDirToken(tokens[idx])) {
        // ---- 格式A: <time> [kw] <ch> <Dir> [FD[x]] <id> ... ----
        frame.direction = (tokens[idx].compare(QLatin1String("Tx"), Qt::CaseInsensitive) == 0)
                          ? CanFrame::Tx : CanFrame::Rx;
        idx++;
        if (idx >= tokens.size()) return false;

        // FD/FDx 标记（无 CAN 关键字时，本软件 AscWriter 格式）
        if (!hasCanKw && tokens[idx].startsWith(QLatin1String("FD"), Qt::CaseInsensitive)) {
            frame.fd = true;
            frame.bitrateSwitch = tokens[idx].contains(QLatin1Char('x'), Qt::CaseInsensitive);
            idx++;
            if (idx >= tokens.size()) return false;
        }
        if (isFdKw) frame.fd = true;

        // ID
        if (!parseCanId(tokens[idx], frame.id, frame.extended)) return false;
        idx++;

        if (hasCanKw && isFdKw) {
            // CANFD: <time> CANFD <ch> <Dir> <id> flags flags [d] dlc码 dataLen data...
            if (idx >= tokens.size()) return false;
            return parseCanFdKwTail();
        }

        // 经典/FD: <time> <ch> <Dir> [FD[x]] <id> <len> [d] data... [BRS] [ESI]
        if (idx >= tokens.size()) return false;
        const int dlc = tokens[idx].toUInt();
        idx++;
        if (idx < tokens.size() &&
            (tokens[idx] == QLatin1String("d") || tokens[idx] == QLatin1String("D")))
            idx++;
        frame.data.clear();
        const int maxData = frame.fd ? 64 : 8;
        for (int i = 0; i < maxData && idx < tokens.size(); ++i) {
            if (!isDataByte(tokens[idx])) break;
            frame.data.append(static_cast<char>(static_cast<quint8>(tokens[idx].toUInt(nullptr, 16))));
            idx++;
        }
        frame.dlc = frame.fd ? CanFrame::lengthToDlc(dlc) : static_cast<quint8>(dlc);

        // 行尾 BRS/ESI 标记（本软件 AscWriter 格式）
        if (frame.fd) {
            for (int i = idx; i < tokens.size(); ++i) {
                if (tokens[i].compare(QLatin1String("BRS"), Qt::CaseInsensitive) == 0)
                    frame.bitrateSwitch = true;
                else if (tokens[i].compare(QLatin1String("ESI"), Qt::CaseInsensitive) == 0)
                    frame.errorState = true;
            }
        }
    } else {
        // ---- 格式B: <time> [kw] <ch> <id> <Dir> ...（ID 在方向之前）----
        if (!parseCanId(tokens[idx], frame.id, frame.extended)) return false;
        idx++;
        if (idx >= tokens.size() || !isDirToken(tokens[idx])) return false;
        frame.direction = (tokens[idx].compare(QLatin1String("Tx"), Qt::CaseInsensitive) == 0)
                          ? CanFrame::Tx : CanFrame::Rx;
        idx++;

        if (hasCanKw && isFdKw) {
            // CANFD: <time> CANFD <ch> <id> <Dir> flags flags [d] dlc码 dataLen data...
            if (idx >= tokens.size()) return false;
            return parseCanFdKwTail();
        }

        if (hasCanKw) {
            // CAN: <time> CAN <ch> <id> <Dir> <dlc> data...
            if (idx >= tokens.size()) return false;
            frame.dlc = static_cast<quint8>(tokens[idx].toUInt());
            idx++;
            frame.data.clear();
            for (int i = 0; i < 8 && idx < tokens.size(); ++i) {
                if (!isDataByte(tokens[idx])) break;
                frame.data.append(static_cast<char>(static_cast<quint8>(tokens[idx].toUInt(nullptr, 16))));
                idx++;
            }
        } else {
            // 无关键字新格式: <time> <ch> <id> <Dir> <type> <dlc> data...（CANoe 15.x 经典）
            if (idx >= tokens.size()) return false;
            const QString &typeStr = tokens[idx];
            idx++;
            bool isRemote = false;
            if (typeStr == QLatin1String("d")) { frame.extended = false; }
            else if (typeStr == QLatin1String("D")) { frame.extended = true; }
            else if (typeStr == QLatin1String("fd")) { frame.fd = true; }
            else if (typeStr == QLatin1String("FD")) { frame.fd = true; frame.bitrateSwitch = true; }
            else if (typeStr == QLatin1String("r")) { isRemote = true; frame.extended = false; }
            else if (typeStr == QLatin1String("R")) { isRemote = true; frame.extended = true; }
            else if (typeStr == QLatin1String("e") || typeStr == QLatin1String("E")) { frame.id |= 0x20000000; }
            else return false;

            if (idx >= tokens.size()) return false;
            const int dlc = tokens[idx].toUInt();
            idx++;
            if (!isRemote) {
                frame.data.clear();
                const int maxData = frame.fd ? 64 : 8;
                for (int i = 0; i < maxData && idx < tokens.size(); ++i) {
                    if (!isDataByte(tokens[idx])) break;
                    frame.data.append(static_cast<char>(static_cast<quint8>(tokens[idx].toUInt(nullptr, 16))));
                    idx++;
                }
            }
            frame.dlc = frame.fd ? CanFrame::lengthToDlc(dlc) : static_cast<quint8>(dlc);
        }
    }

    return true;
}

} // namespace

int AscReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    QTextStream ts(&m_file);
    ts.setEncoding(QStringConverter::Utf8);

    // 头部/结构性行跳过：date / base / [no ]internal events / Begin|End
    // TriggerBlock / 注释（覆盖本软件与 CANoe 7.x/15.x 导出头部）
    static const QRegularExpression skipPattern(
        QStringLiteral("^(date|base|internal|no internal|begin|end|//|;)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sep(QStringLiteral("\\s+"));

    double baseTime = 0.0;
    bool hasBaseTime = false;
    int count = 0;

    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (skipPattern.match(line).hasMatch())
            continue;

        CanFrame frame;
        if (!parseFrameLine(line.split(sep, Qt::SkipEmptyParts), frame))
            continue;

        if (!hasBaseTime) {
            baseTime = frame.timestamp;
            hasBaseTime = true;
        }
        frame.timestamp -= baseTime;

        frames.append(frame);
        ++count;
    }

    return count;
}

void AscReader::close()
{
    m_file.close();
}
