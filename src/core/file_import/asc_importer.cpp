#include "asc_importer.h"

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

    qDebug() << "ASC 导入完成:" << frames.size() << "帧, 跳过" << skipped << "行";
    return frames;
}

bool AscImporter::parseLine(const QString &line, CanFrame &frame)
{
    // 按空白分割
    static const QRegularExpression sep(QStringLiteral("\\s+"));
    const auto tokens = line.split(sep, Qt::SkipEmptyParts);
    if (tokens.size() < 5)
        return false;

    // 第一个 token 一定是时间戳
    bool ok = false;
    frame.timestamp = tokens[0].toDouble(&ok);
    if (!ok) return false;

    // ---- 判断格式 ----
    // 旧格式: "0.001  CAN  1  Rx  0123  8  01 02 ..."
    //         tokens[1] == "CAN" 或 "CANFD"
    // 新格式: "0.001000 1  0x200  Rx  d 8 01 02 ..."
    //         tokens[1] 是数字（通道号）

    if (tokens.size() > 2 &&
        (tokens[1].compare(QStringLiteral("CAN"), Qt::CaseInsensitive) == 0 ||
         tokens[1].compare(QStringLiteral("CANFD"), Qt::CaseInsensitive) == 0)) {
        // ============ 旧格式 ============
        // tokens: [0]=time [1]=CAN/CANFD [2]=channel [3]=dir [4]=id [5]=dlc [6+]=data
        if (tokens.size() < 6) return false;

        const bool isFd = (tokens[1].compare(QStringLiteral("CANFD"), Qt::CaseInsensitive) == 0);

        // 通道
        frame.channel = static_cast<quint8>(tokens[2].toUInt());

        // 方向
        if (tokens[3].compare(QStringLiteral("Rx"), Qt::CaseInsensitive) == 0)
            frame.direction = CanFrame::Rx;
        else if (tokens[3].compare(QStringLiteral("Tx"), Qt::CaseInsensitive) == 0)
            frame.direction = CanFrame::Tx;
        else
            return false;

        // ID（hex，可能带 'x' 后缀表示扩展帧）
        QString idStr = tokens[4];
        if (idStr.endsWith(QLatin1Char('x'), Qt::CaseInsensitive)) {
            frame.extended = true;
            idStr.chop(1);
        }
        if (idStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            idStr = idStr.mid(2);
        frame.id = idStr.toUInt(&ok, 16);
        if (!ok) return false;

        // DLC
        frame.dlc = static_cast<quint8>(tokens[5].toUInt());
        frame.fd = isFd;

        // 数据字节
        frame.data.clear();
        for (int i = 6; i < tokens.size(); ++i) {
            bool ok2;
            const quint8 byte = static_cast<quint8>(tokens[i].toUInt(&ok2, 16));
            if (ok2)
                frame.data.append(static_cast<char>(byte));
        }

        return true;
    }

    // ============ 新格式 ============
    // tokens: [0]=time [1]=channel [2]=id [3]=dir [4]=type [5]=dlc [6+]=data
    if (tokens.size() < 5) return false;

    // 通道
    frame.channel = static_cast<quint8>(tokens[1].toUInt());

    // ID（hex，可能带 0x 前缀和 'x' 后缀）
    QString idStr = tokens[2];
    if (idStr.endsWith(QLatin1Char('x'), Qt::CaseInsensitive)) {
        frame.extended = true;
        idStr.chop(1);
    }
    if (idStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        idStr = idStr.mid(2);
    frame.id = idStr.toUInt(&ok, 16);
    if (!ok) return false;

    // 方向
    if (tokens[3].compare(QStringLiteral("Rx"), Qt::CaseInsensitive) == 0)
        frame.direction = CanFrame::Rx;
    else if (tokens[3].compare(QStringLiteral("Tx"), Qt::CaseInsensitive) == 0)
        frame.direction = CanFrame::Tx;
    else
        return false;

    if (tokens.size() < 6) return false;

    // 类型指示符
    //   d  = 经典标准帧
    //   D  = 经典扩展帧
    //   fd = CAN FD（无 BRS）
    //   FD = CAN FD + BRS
    //   r  = 标准远程帧
    //   R  = 扩展远程帧
    //   e  = 错误帧
    const QString &typeStr = tokens[4];
    bool isRemote = false;

    if (typeStr == QLatin1String("d")) {
        frame.extended = false;
    } else if (typeStr == QLatin1String("D")) {
        frame.extended = true;
    } else if (typeStr == QLatin1String("fd")) {
        frame.fd = true;
    } else if (typeStr == QLatin1String("FD")) {
        frame.fd = true;
        frame.bitrateSwitch = true;
    } else if (typeStr == QLatin1String("r")) {
        isRemote = true;
        frame.extended = false;
    } else if (typeStr == QLatin1String("R")) {
        isRemote = true;
        frame.extended = true;
    } else if (typeStr == QLatin1String("e") || typeStr == QLatin1String("E")) {
        // 错误帧
        frame.id |= 0x20000000;
    } else {
        return false; // 未知类型
    }

    // DLC
    frame.dlc = static_cast<quint8>(tokens[5].toUInt());

    // 数据字节（远程帧无数据）
    if (!isRemote) {
        frame.data.clear();
        for (int i = 6; i < tokens.size(); ++i) {
            bool ok2;
            const quint8 byte = static_cast<quint8>(tokens[i].toUInt(&ok2, 16));
            if (ok2)
                frame.data.append(static_cast<char>(byte));
        }
    }

    return true;
}
