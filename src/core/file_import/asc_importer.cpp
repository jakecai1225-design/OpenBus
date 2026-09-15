#include "asc_importer.h"
#include "utils/canutils.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

QVector<CanFrame> AscImporter::importFile(
    const QString &filePath,
    std::function<void(double)> progress)
{
    QVector<CanFrame> frames;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "ASC: 无法打开文件" << filePath;
        return frames;
    }

    const qint64 fileSize = file.size();
    // 预分配空间（估算：平均每行约 40 字节）
    if (fileSize > 0)
        frames.reserve(static_cast<int>(fileSize / 40));

    QTextStream in(&file);
    QString line;
    int lineNum = 0;
    int skipped = 0;

    // 匹配需要跳过的头部/结构性行
    // date / base / internal events / begin Triggerblock / end Triggerblock
    static const QRegularExpression skipPattern(
        QStringLiteral("^(date|base|internal|begin|end|//|;)"),
        QRegularExpression::CaseInsensitiveOption);

    while (in.readLineInto(&line)) {
        ++lineNum;

        // 进度回调（每 1000 行更新一次以减少开销）
        if (progress && fileSize > 0 && (lineNum % 1000 == 0)) {
            progress(static_cast<double>(file.pos()) / fileSize);
        }

        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) continue;

        // 跳过注释和结构性行
        if (skipPattern.match(trimmed).hasMatch()) {
            ++skipped;
            continue;
        }

        CanFrame frame;
        if (parseLine(trimmed, frame)) {
            frames.append(frame);
        } else {
            ++skipped;
        }
    }

    if (progress) progress(1.0);

    // Same Trace/Graphic axis as CanFileIO ASC reader (first frame = 0)
    CanUtils::makeRelativeToFirst(frames);

    qDebug() << "ASC import done:" << frames.size() << "frames, skipped" << skipped << "lines";
    return frames;
}

/// 判断 token 是否为方向标记
static inline bool isDirToken(const QString &s)
{
    return s.compare(QStringLiteral("Rx"), Qt::CaseInsensitive) == 0 ||
           s.compare(QStringLiteral("Tx"), Qt::CaseInsensitive) == 0;
}

/// 从 token 解析 CAN ID（支持 0x 前缀和 x 后缀）
static inline bool parseCanId(QString token, quint32 &id, bool &extended)
{
    extended = false;
    if (token.endsWith(QLatin1Char('x'), Qt::CaseInsensitive)) {
        extended = true;
        token.chop(1);
    }
    if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        token = token.mid(2);
    bool ok = false;
    id = token.toUInt(&ok, 16);
    return ok;
}

/// 判断 token 是否为有效数据字节（1~2 位十六进制）
static inline bool isDataByte(const QString &t)
{
    if (t.length() < 1 || t.length() > 2)
        return false;
    bool ok = false;
    t.toUInt(&ok, 16);
    return ok;
}

bool AscImporter::parseLine(const QString &line, CanFrame &frame)
{
    static const QRegularExpression sep(QStringLiteral("\\s+"));
    const auto tokens = line.split(sep, Qt::SkipEmptyParts);
    if (tokens.size() < 5)
        return false;

    // 时间戳
    bool ok = false;
    frame.timestamp = tokens[0].toDouble(&ok);
    if (!ok) return false;

    int idx = 1;

    // 可选 CAN/CANFD 关键字
    bool hasCanKw = false;
    bool isFdKw = false;
    if (idx < tokens.size() && tokens[idx].compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0) {
        hasCanKw = true;
        idx++;
    } else if (idx < tokens.size() && tokens[idx].compare(QStringLiteral("CANFD"), Qt::CaseInsensitive) == 0) {
        hasCanKw = true;
        isFdKw = true;
        idx++;
    }

    if (idx >= tokens.size()) return false;

    // 通道
    frame.channel = static_cast<quint8>(tokens[idx].toUInt());
    idx++;

    if (idx >= tokens.size()) return false;

    // 根据方向标记位置判断格式
    if (isDirToken(tokens[idx])) {
        // ---- 格式A: ... channel DIR [FD[x]] id dlc [d] data... [BRS] [ESI] ----
        // ---- 或 CANFD: ... CANFD channel DIR id flags flags [d] dlc_code data_len data... ----
        frame.direction = (tokens[idx].compare(QStringLiteral("Tx"), Qt::CaseInsensitive) == 0)
                          ? CanFrame::Tx : CanFrame::Rx;
        idx++;
        if (idx >= tokens.size()) return false;

        // 检查 FD/FDx 标记（无 CAN 关键字时）
        if (!hasCanKw && tokens[idx].startsWith(QStringLiteral("FD"), Qt::CaseInsensitive)) {
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
            // CANFD: ... id flags1 flags2 [d] dlc_code data_len data...
            idx++; // flags1
            idx++; // flags2
            if (idx >= tokens.size()) return false;
            if (tokens[idx] == QLatin1String("d") || tokens[idx] == QLatin1String("D")) idx++;
            if (idx >= tokens.size()) return false;
            frame.dlc = static_cast<quint8>(tokens[idx].toUInt(nullptr, 16));
            idx++;
            if (idx >= tokens.size()) return false;
            int dataLen = tokens[idx].toUInt();
            idx++;
            frame.data.clear();
            for (int i = 0; i < dataLen && idx < tokens.size(); ++i) {
                bool ok2; quint8 b = static_cast<quint8>(tokens[idx].toUInt(&ok2, 16));
                if (ok2) frame.data.append(static_cast<char>(b)); idx++;
            }
        } else {
            // 经典/FD: ... id dlc [d] data... [BRS] [ESI]
            int dlc = tokens[idx].toUInt(); idx++;
            if (idx < tokens.size() && (tokens[idx] == QLatin1String("d") || tokens[idx] == QLatin1String("D"))) idx++;
            frame.data.clear();
            int maxData = frame.fd ? 64 : 8;
            for (int i = 0; i < maxData && idx < tokens.size(); ++i) {
                if (!isDataByte(tokens[idx])) break;
                frame.data.append(static_cast<char>(static_cast<quint8>(tokens[idx].toUInt(nullptr, 16))));
                idx++;
            }
            if (frame.fd) frame.dlc = CanFrame::lengthToDlc(dlc);
            else frame.dlc = static_cast<quint8>(dlc);
        }
    } else {
        // ---- 格式B: ... channel ID dir ... ----
        // ID 在方向之前
        if (!parseCanId(tokens[idx], frame.id, frame.extended)) return false;
        idx++;
        if (idx >= tokens.size() || !isDirToken(tokens[idx])) return false;
        frame.direction = (tokens[idx].compare(QStringLiteral("Tx"), Qt::CaseInsensitive) == 0)
                          ? CanFrame::Tx : CanFrame::Rx;
        idx++;

        if (hasCanKw && isFdKw) {
            // CANFD: ... id dir flags1 flags2 [d] dlc_code data_len data...
            idx++; idx++; // flags1, flags2
            if (idx >= tokens.size()) return false;
            if (tokens[idx] == QLatin1String("d") || tokens[idx] == QLatin1String("D")) idx++;
            if (idx >= tokens.size()) return false;
            frame.dlc = static_cast<quint8>(tokens[idx].toUInt(nullptr, 16)); idx++;
            if (idx >= tokens.size()) return false;
            int dataLen = tokens[idx].toUInt(); idx++;
            frame.fd = true;
            frame.data.clear();
            for (int i = 0; i < dataLen && idx < tokens.size(); ++i) {
                bool ok2; quint8 b = static_cast<quint8>(tokens[idx].toUInt(&ok2, 16));
                if (ok2) frame.data.append(static_cast<char>(b)); idx++;
            }
        } else if (hasCanKw) {
            // CAN: ... id dir dlc data...
            if (idx >= tokens.size()) return false;
            frame.dlc = static_cast<quint8>(tokens[idx].toUInt()); idx++;
            frame.data.clear();
            for (int i = 0; i < 8 && idx < tokens.size(); ++i) {
                if (!isDataByte(tokens[idx])) break;
                frame.data.append(static_cast<char>(static_cast<quint8>(tokens[idx].toUInt(nullptr, 16))));
                idx++;
            }
        } else {
            // 新格式: ... id dir type dlc data...
            if (idx >= tokens.size()) return false;
            const QString &typeStr = tokens[idx]; idx++;
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
            int dlc = tokens[idx].toUInt(); idx++;
            if (!isRemote) {
                frame.data.clear();
                int maxData = frame.fd ? 64 : 8;
                for (int i = 0; i < maxData && idx < tokens.size(); ++i) {
                    if (!isDataByte(tokens[idx])) break;
                    frame.data.append(static_cast<char>(static_cast<quint8>(tokens[idx].toUInt(nullptr, 16))));
                    idx++;
                }
            }
            if (frame.fd) frame.dlc = CanFrame::lengthToDlc(dlc);
            else frame.dlc = static_cast<quint8>(dlc);
        }
    }

    return true;
}
