#include "csv_importer.h"
#include "utils/canutils.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

QVector<CanFrame> CsvImporter::importFile(
    const QString &filePath,
    std::function<void(double)> progress)
{
    QVector<CanFrame> frames;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "CSV: 无法打开文件" << filePath;
        return frames;
    }

    const qint64 fileSize = file.size();
    if (fileSize > 0)
        frames.reserve(static_cast<int>(fileSize / 50));

    QTextStream in(&file);
    QString line;
    int lineNum = 0;

    // 第一行：判断是否有表头
    if (!in.readLineInto(&line))
        return frames;

    ColumnMap colMap = parseHeader(line.trimmed());
    bool hasHeader = (colMap.time >= 0 || colMap.id >= 0);

    // 如果第一行不是表头，则作为数据行处理
    if (!hasHeader) {
        // 无表头，使用默认列顺序：Time,Channel,ID,Dir,DLC,Data
        ColumnMap defaultMap;
        defaultMap.time = 0;
        defaultMap.channel = 1;
        defaultMap.id = 2;
        defaultMap.dir = 3;
        defaultMap.dlc = 4;
        defaultMap.data = 5;

        CanFrame frame;
        const auto fields = line.split(QLatin1Char(','));
        if (buildFrame(fields, defaultMap, frame))
            frames.append(frame);
        colMap = defaultMap;
    }

    while (in.readLineInto(&line)) {
        ++lineNum;

        if (progress && fileSize > 0 && (lineNum % 1000 == 0)) {
            progress(static_cast<double>(file.pos()) / fileSize);
        }

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        CanFrame frame;
        const auto fields = trimmed.split(QLatin1Char(','));
        if (buildFrame(fields, colMap, frame))
            frames.append(frame);
    }

    if (progress) progress(1.0);

    // Same Trace/Graphic axis as CanFileIO CSV reader (first frame = 0)
    CanUtils::makeRelativeToFirst(frames);

    qDebug() << "CSV import done:" << frames.size() << "frames";
    return frames;
}

CsvImporter::ColumnMap CsvImporter::parseHeader(const QString &headerLine)
{
    ColumnMap map;
    const auto fields = headerLine.split(QLatin1Char(','));

    for (int i = 0; i < fields.size(); ++i) {
        QString name = fields[i].trimmed().toLower();

        if (name == QLatin1String("time") || name == QLatin1String("timestamp") ||
            name == QStringLiteral("时间"))
            map.time = i;
        else if (name == QLatin1String("channel") || name == QLatin1String("ch") ||
                 name == QStringLiteral("通道"))
            map.channel = i;
        else if (name == QLatin1String("id") || name == QLatin1String("canid") ||
                 name == QLatin1String("arbid"))
            map.id = i;
        else if (name == QLatin1String("dir") || name == QLatin1String("direction") ||
                 name == QStringLiteral("方向"))
            map.dir = i;
        else if (name == QLatin1String("extended") || name == QLatin1String("ext"))
            map.extended = i;
        else if (name == QLatin1String("fd") || name == QLatin1String("canfd"))
            map.fd = i;
        else if (name == QLatin1String("dlc") || name == QLatin1String("length") ||
                 name == QLatin1String("len"))
            map.dlc = i;
        else if (name == QLatin1String("data") || name == QLatin1String("payload") ||
                 name == QStringLiteral("数据"))
            map.data = i;
    }

    return map;
}

bool CsvImporter::buildFrame(const QStringList &fields, const ColumnMap &map, CanFrame &frame)
{
    bool ok = false;

    // 时间戳
    if (map.time >= 0 && map.time < fields.size()) {
        frame.timestamp = fields[map.time].trimmed().toDouble(&ok);
        if (!ok) return false;
    }

    // 通道
    if (map.channel >= 0 && map.channel < fields.size()) {
        frame.channel = static_cast<quint8>(fields[map.channel].trimmed().toUInt());
    }

    // ID
    if (map.id >= 0 && map.id < fields.size()) {
        QString idStr = fields[map.id].trimmed();
        if (idStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            idStr = idStr.mid(2);
        frame.id = idStr.toUInt(&ok, 16);
        if (!ok) {
            // 尝试十进制
            frame.id = idStr.toUInt();
        }
    }

    // 方向
    if (map.dir >= 0 && map.dir < fields.size()) {
        QString dirStr = fields[map.dir].trimmed().toLower();
        if (dirStr == QLatin1String("rx") || dirStr == QLatin1String("r"))
            frame.direction = CanFrame::Rx;
        else if (dirStr == QLatin1String("tx") || dirStr == QLatin1String("t"))
            frame.direction = CanFrame::Tx;
    }

    // 扩展帧标志
    if (map.extended >= 0 && map.extended < fields.size()) {
        QString extStr = fields[map.extended].trimmed().toLower();
        frame.extended = (extStr == QLatin1String("true") || extStr == QLatin1String("1") ||
                          extStr == QLatin1String("yes"));
    }

    // CAN FD 标志
    if (map.fd >= 0 && map.fd < fields.size()) {
        QString fdStr = fields[map.fd].trimmed().toLower();
        frame.fd = (fdStr == QLatin1String("true") || fdStr == QLatin1String("1") ||
                    fdStr == QLatin1String("yes"));
    }

    // DLC
    if (map.dlc >= 0 && map.dlc < fields.size()) {
        frame.dlc = static_cast<quint8>(fields[map.dlc].trimmed().toUInt());
    }

    // 数据
    if (map.data >= 0 && map.data < fields.size()) {
        frame.data.clear();
        // 数据可能是空格分隔的 hex 字节，也可能是连续 hex 字符串
        QString dataStr = fields[map.data].trimmed();
        // 去除可能的引号
        if (dataStr.startsWith(QLatin1Char('"')) && dataStr.endsWith(QLatin1Char('"')))
            dataStr = dataStr.mid(1, dataStr.size() - 2);

        const auto bytes = dataStr.split(QRegularExpression(QStringLiteral("\\s+")),
                                          Qt::SkipEmptyParts);
        for (const auto &byteStr : bytes) {
            bool ok2;
            const quint8 byte = static_cast<quint8>(byteStr.toUInt(&ok2, 16));
            if (ok2)
                frame.data.append(static_cast<char>(byte));
        }

        // 如果没解析出数据（可能没有空格分隔），尝试每两个字符一组
        if (frame.data.isEmpty() && dataStr.size() >= 2) {
            dataStr.remove(QLatin1Char(' '));
            for (int i = 0; i + 1 < dataStr.size(); i += 2) {
                bool ok2;
                const quint8 byte = static_cast<quint8>(
                    dataStr.mid(i, 2).toUInt(&ok2, 16));
                if (ok2)
                    frame.data.append(static_cast<char>(byte));
            }
        }
    }

    // 如果 DLC 未设置，根据数据长度推断
    if (frame.dlc == 0 && !frame.data.isEmpty()) {
        frame.dlc = CanFrame::lengthToDlc(frame.data.size());
    }

    return true;
}
