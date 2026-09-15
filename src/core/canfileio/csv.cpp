#include "csv.h"
#include "core/canframe.h"

#include <QStringList>

// ============================================================
//  CsvWriter
// ============================================================

CsvWriter::CsvWriter() = default;

CsvWriter::~CsvWriter()
{
    close();
}

bool CsvWriter::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    m_stream.setDevice(&m_file);
    m_stream.setEncoding(QStringConverter::Utf8);
    m_frameCount = 0;

    // CSV 表头
    m_stream << "timestamp,channel,direction,id,extended,fd,brs,esi,dlc,data\n";
    return true;
}

void CsvWriter::writeFrame(const CanFrame &frame)
{
    if (!isOpen())
        return;

    m_stream << QString::number(frame.timestamp, 'f', 6) << ",";
    m_stream << QString::number(frame.channel) << ",";
    m_stream << (frame.direction == CanFrame::Tx ? "Tx" : "Rx") << ",";
    m_stream << QString("0x%1").arg(frame.id, 0, 16, QChar('0')).toUpper() << ",";
    m_stream << (frame.extended ? "1" : "0") << ",";
    m_stream << (frame.fd ? "1" : "0") << ",";
    m_stream << (frame.bitrateSwitch ? "1" : "0") << ",";
    m_stream << (frame.errorState ? "1" : "0") << ",";
    m_stream << QString::number(frame.dlc) << ",";

    // 数据字节以空格分隔，整体放在引号中避免逗号冲突
    m_stream << "\"";
    for (int i = 0; i < frame.data.size(); ++i) {
        if (i > 0) m_stream << " ";
        m_stream << QString("%1").arg(static_cast<unsigned char>(frame.data[i]), 2, 16, QChar('0')).toUpper();
    }
    m_stream << "\"\n";

    if (++m_frameCount % 1000 == 0)
        m_stream.flush();
}

void CsvWriter::close()
{
    if (!isOpen())
        return;
    m_stream.flush();
    m_file.close();
}

// ============================================================
//  CsvReader
// ============================================================

CsvReader::CsvReader() = default;

CsvReader::~CsvReader()
{
    close();
}

bool CsvReader::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    return m_file.open(QIODevice::ReadOnly | QIODevice::Text);
}

int CsvReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    QTextStream ts(&m_file);
    ts.setEncoding(QStringConverter::Utf8);

    int count = 0;
    bool hasHeader = false;
    double baseTime = 0.0;
    bool hasBase = false;

    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty())
            continue;

        // Skip header
        if (!hasHeader) {
            if (line.toLower().startsWith("timestamp"))
                hasHeader = true;
            continue;
        }

        // Format: timestamp,channel,direction,id,extended,fd,brs,esi,dlc,"data"
        QStringList parts;
        bool inQuote = false;
        int start = 0;
        for (int i = 0; i <= line.size(); ++i) {
            if (i == line.size() || (line[i] == ',' && !inQuote)) {
                parts.append(line.mid(start, i - start));
                start = i + 1;
            } else if (line[i] == '"') {
                inQuote = !inQuote;
            }
        }

        if (parts.size() < 9)
            continue;

        CanFrame frame;
        bool ok = false;

        frame.timestamp = parts[0].toDouble(&ok);
        if (!ok) continue;

        frame.channel = static_cast<quint8>(parts[1].toUInt(&ok));
        if (!ok) frame.channel = 1;

        frame.direction = (parts[2].toUpper() == "TX") ? CanFrame::Tx : CanFrame::Rx;

        QString idStr = parts[3];
        if (idStr.startsWith("0x", Qt::CaseInsensitive))
            idStr = idStr.mid(2);
        frame.id = idStr.toUInt(&ok, 16);
        if (!ok) frame.id = 0;

        frame.extended = parts[4].toInt() != 0;
        frame.fd = parts[5].toInt() != 0;
        frame.bitrateSwitch = parts[6].toInt() != 0;
        frame.errorState = parts[7].toInt() != 0;
        frame.dlc = static_cast<quint8>(parts[8].toUInt(&ok));
        if (!ok) frame.dlc = 0;

        if (parts.size() > 9) {
            QString dataStr = parts[9];
            dataStr.remove('"');
            QStringList bytes = dataStr.split(' ', Qt::SkipEmptyParts);
            for (const auto &b : bytes) {
                bool hexOk = false;
                unsigned int val = b.toUInt(&hexOk, 16);
                if (hexOk && val <= 0xFF)
                    frame.data.append(static_cast<char>(val));
            }
        }

        // Relative timestamps (same as ASC/BLF/TRC/PCAP) so Graphic viewport [0, window] works
        if (!keepAbsoluteTimestamps()) {
            if (!hasBase) {
                baseTime = frame.timestamp;
                hasBase = true;
            }
            frame.timestamp -= baseTime;
        }
        if (frame.timestampNs == 0)
            frame.timestampNs = static_cast<quint64>(frame.timestamp * 1e9 + 0.5);

        frames.append(frame);
        ++count;
    }

    return count;
}

void CsvReader::close()
{
    m_file.close();
}
