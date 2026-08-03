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

    // CAN ID
    QString idStr = QString("%1")
        .arg(frame.id & (frame.extended ? 0x1FFFFFFF : 0x7FF),
             frame.extended ? 8 : 3, 16, QChar('0')).toUpper();
    if (frame.extended)
        m_stream << "x" << idStr;
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

int AscReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    QTextStream ts(&m_file);
    ts.setEncoding(QStringConverter::Utf8);

    double baseTime = 0.0;
    bool hasBaseTime = false;
    int count = 0;

    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty())
            continue;

        // 跳过文件头行
        if (line.startsWith("date", Qt::CaseInsensitive) ||
            line.startsWith("base", Qt::CaseInsensitive) ||
            line.startsWith("no internal", Qt::CaseInsensitive) ||
            line.startsWith("//", Qt::CaseInsensitive)) {
            continue;
        }

        // 解析帧行
        // 格式: <timestamp> <channel> <Rx|Tx> [FD[x]] <ID>[x] <dlc> [d] <data...>
        // 示例: 123.456789 1 Rx 123 8 01 02 03 04 05 06 07 08
        //        456.789012 2 Tx FDx 00000123 64 d 01 02 ...

        CanFrame frame;
        frame.direction = CanFrame::Rx;
        frame.fd = false;
        frame.extended = false;

        // 时间戳
        int pos = 0;
        while (pos < line.size() && line[pos].isSpace()) ++pos;
        int start = pos;
        while (pos < line.size() && !line[pos].isSpace()) ++pos;
        bool ok = false;
        frame.timestamp = line.mid(start, pos - start).toDouble(&ok);
        if (!ok) continue;

        // 通道号
        while (pos < line.size() && line[pos].isSpace()) ++pos;
        start = pos;
        while (pos < line.size() && !line[pos].isSpace()) ++pos;
        frame.channel = static_cast<quint8>(line.mid(start, pos - start).toUInt(&ok));
        if (!ok) continue;

        // 方向
        while (pos < line.size() && line[pos].isSpace()) ++pos;
        start = pos;
        while (pos < line.size() && !line[pos].isSpace()) ++pos;
        QString dirStr = line.mid(start, pos - start).toUpper();
        if (dirStr == "TX") frame.direction = CanFrame::Tx;
        else frame.direction = CanFrame::Rx;

        // 检查 FD 标记
        while (pos < line.size() && line[pos].isSpace()) ++pos;
        start = pos;
        while (pos < line.size() && !line[pos].isSpace()) ++pos;
        QString token = line.mid(start, pos - start);

        if (token.toUpper().startsWith("FD")) {
            frame.fd = true;
            frame.bitrateSwitch = token.contains("x", Qt::CaseInsensitive);

            // 下一个 token 是 ID
            while (pos < line.size() && line[pos].isSpace()) ++pos;
            start = pos;
            while (pos < line.size() && !line[pos].isSpace()) ++pos;
            token = line.mid(start, pos - start);
        }

        // 解析 ID
        if (token.toLower().endsWith("x")) {
            frame.extended = true;
            frame.id = token.left(token.size() - 1).toUInt(nullptr, 16);
        } else {
            frame.id = token.toUInt(nullptr, 16);
        }

        // DLC
        while (pos < line.size() && line[pos].isSpace()) ++pos;
        start = pos;
        while (pos < line.size() && !line[pos].isSpace()) ++pos;
        int dlcOrLen = line.mid(start, pos - start).toInt(&ok);
        if (!ok) continue;

        if (frame.fd) {
            frame.dlc = CanFrame::lengthToDlc(dlcOrLen);
        } else {
            frame.dlc = static_cast<quint8>(dlcOrLen);
        }

        // 跳过 'd' 标记（某些 ASC 变体中 FD 帧数据前有 'd'）
        while (pos < line.size() && line[pos].isSpace()) ++pos;
        if (pos < line.size() && (line[pos] == 'd' || line[pos] == 'D')) {
            // 检查是否是数据标记还是数据字节
            if (pos + 1 < line.size() && line[pos + 1].isSpace()) {
                ++pos; // 跳过 'd'
                while (pos < line.size() && line[pos].isSpace()) ++pos;
            }
        }

        // 读取数据字节
        int dataLen = frame.fd ? CanFrame::dlcToLength(frame.dlc) : frame.dlc;
        dataLen = qMin(dataLen, 64);
        frame.data.resize(dataLen);

        for (int i = 0; i < dataLen; ++i) {
            start = pos;
            while (pos < line.size() && !line[pos].isSpace()) ++pos;
            bool hexOk = false;
            frame.data[i] = static_cast<char>(line.mid(start, pos - start).toUInt(&hexOk, 16));
            if (!hexOk) {
                frame.data.resize(i);
                break;
            }
            while (pos < line.size() && line[pos].isSpace()) ++pos;
        }

        // 检查行尾的 BRS/ESI 标记
        if (frame.fd && pos < line.size()) {
            QString rest = line.mid(pos).toUpper();
            if (rest.contains("BRS"))
                frame.bitrateSwitch = true;
            if (rest.contains("ESI"))
                frame.errorState = true;
        }

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
