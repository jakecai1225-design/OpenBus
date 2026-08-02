#include "blf_importer.h"

#include <QFile>
#include <QDataStream>
#include <QByteArray>
#include <QDebug>

// ============================================================
//  BLF 二进制格式常量
// ============================================================
namespace {
    // 文件头 magic "LOGG" (little-endian uint32)
    constexpr quint32 FILE_MAGIC = 0x47474F4C;
    // 对象头 magic "LOBJ" (little-endian uint32)
    constexpr quint32 OBJ_MAGIC  = 0x4A424F4C;

    // 对象类型
    constexpr quint32 OBJ_LOG_CONTAINER     = 10;
    constexpr quint32 OBJ_CAN_MESSAGE       = 1;
    constexpr quint32 OBJ_CAN_MESSAGE2      = 86;
    constexpr quint32 OBJ_CAN_FD_MESSAGE    = 100;
    constexpr quint32 OBJ_CAN_FD_MESSAGE_64 = 101;

    // CAN 帧标志位
    constexpr quint8 FLAG_TX       = 0x01;
    // constexpr quint8 FLAG_RTR      = 0x10;  // 未使用
    constexpr quint8 FLAG_EXTENDED = 0x20;
    constexpr quint8 FLAG_BRS      = 0x40;
    constexpr quint8 FLAG_ESI      = 0x80;

    /// 读取一个 BLF 对象头（16 字节基础部分）
    struct ObjHeader {
        quint32 magic;
        quint16 headerSize;
        quint16 headerVersion;
        quint32 objectSize;
        quint32 objectType;
    };

    bool readObjHeader(QDataStream &in, ObjHeader &h)
    {
        in >> h.magic >> h.headerSize >> h.headerVersion >> h.objectSize >> h.objectType;
        if (in.status() != QDataStream::Ok || h.magic != OBJ_MAGIC)
            return false;
        // headerSize 至少 16，objectSize 至少等于 headerSize
        if (h.headerSize < 16 || h.objectSize < h.headerSize)
            return false;
        return true;
    }

    /// 从数据流中解析 CAN 帧
    /// CanMessage 结构 (packed, 24 字节):
    ///   uint16 channel | uint8 flags | uint8 dlc | uint32 id | uint8 data[8] | uint64 timestamp
    /// CanFdMessage 结构 (packed, 80 字节):
    ///   uint16 channel | uint8 flags | uint8 dlc | uint32 id | uint8 data[64] | uint64 timestamp
    CanFrame parseCanFrame(const char *raw, int rawLen, bool isFd)
    {
        CanFrame frame;
        if (rawLen < (isFd ? 80 : 24))
            return frame;

        // 读取字段 (little-endian, packed)
        quint16 channel;
        quint8  flags, dlc;
        quint32 id;

        memcpy(&channel, raw + 0, 2);
        memcpy(&flags,   raw + 2, 1);
        memcpy(&dlc,     raw + 3, 1);
        memcpy(&id,      raw + 4, 4);

        quint64 timestamp = 0;
        if (isFd)
            memcpy(&timestamp, raw + 72, 8);  // 4 + 64 = 68? No: 2+1+1+4=8, +64=72
        else
            memcpy(&timestamp, raw + 16, 8);  // 2+1+1+4=8, +8=16

        frame.channel   = static_cast<quint8>(channel);
        frame.id        = id;
        frame.dlc       = dlc;
        frame.extended  = (flags & FLAG_EXTENDED) != 0;
        frame.direction = (flags & FLAG_TX) ? CanFrame::Tx : CanFrame::Rx;

        if (isFd) {
            frame.fd = true;
            frame.bitrateSwitch = (flags & FLAG_BRS) != 0;
            frame.errorState    = (flags & FLAG_ESI) != 0;
            int actualLen = CanFrame::dlcToLength(dlc);
            frame.data = QByteArray(raw + 8, qMin(actualLen, 64));
        } else {
            int dataLen = qMin(static_cast<int>(dlc), 8);
            frame.data = QByteArray(raw + 8, dataLen);
        }

        // BLF 时间戳: 1/10 微秒，绝对时间（自 2007-01-01）
        frame.timestamp = static_cast<double>(timestamp) / 1e7;
        return frame;
    }

    /// 解压 Log Container 数据
    /// compressionMethod: 0=未压缩, 1=zlib, 2=raw deflate
    QByteArray decompressContainer(const QByteArray &compressed,
                                   quint32 uncompressedSize,
                                   quint8 compressionMethod)
    {
        if (compressionMethod == 0) {
            // 未压缩
            return compressed;
        }

        if (compressionMethod == 1) {
            // zlib 格式 — Qt 的 qUncompress 期望 [4字节大端size] + [zlib数据]
            QByteArray wrapped;
            wrapped.resize(4 + compressed.size());
            wrapped[0] = (uncompressedSize >> 24) & 0xFF;
            wrapped[1] = (uncompressedSize >> 16) & 0xFF;
            wrapped[2] = (uncompressedSize >> 8)  & 0xFF;
            wrapped[3] =  uncompressedSize        & 0xFF;
            memcpy(wrapped.data() + 4, compressed.constData(), compressed.size());
            QByteArray result = qUncompress(wrapped);
            if (result.isEmpty())
                qWarning() << "BLF: qUncompress 失败, compressedSize=" << compressed.size()
                           << "uncompressedSize=" << uncompressedSize;
            return result;
        }

        if (compressionMethod == 2) {
            // raw deflate — 构造 zlib 包装后用 qUncompress
            // zlib header: 0x78 0x9C (default compression)
            // 需要 Adler-32 校验和，但 qUncompress 内部用 uncompress() 会校验
            // 尝试直接加 header 看能否解压
            QByteArray wrapped;
            wrapped.resize(4 + 2 + compressed.size() + 4);
            wrapped[0] = (uncompressedSize >> 24) & 0xFF;
            wrapped[1] = (uncompressedSize >> 16) & 0xFF;
            wrapped[2] = (uncompressedSize >> 8)  & 0xFF;
            wrapped[3] =  uncompressedSize        & 0xFF;
            wrapped[4] = 0x78;  // zlib header byte 1
            wrapped[5] = 0x9C;  // zlib header byte 2
            memcpy(wrapped.data() + 6, compressed.constData(), compressed.size());
            // Adler-32 of empty data (placeholder — uncompress may still fail)
            wrapped[wrapped.size() - 4] = 0x00;
            wrapped[wrapped.size() - 3] = 0x00;
            wrapped[wrapped.size() - 2] = 0x00;
            wrapped[wrapped.size() - 1] = 0x01;
            QByteArray result = qUncompress(wrapped);
            if (result.isEmpty())
                qWarning() << "BLF: raw deflate 解压失败, compressedSize=" << compressed.size();
            return result;
        }

        qWarning() << "BLF: 未知压缩方法" << compressionMethod;
        return {};
    }

    /// 解析容器内或顶层的 CAN 帧对象
    void parseCanObjects(const QByteArray &data, QVector<CanFrame> &result,
                         bool &firstFrame, double &baseTimestamp)
    {
        const char *raw = data.constData();
        int pos = 0;

        while (pos + 16 <= data.size()) {
            // 读取对象头
            ObjHeader coh;
            memcpy(&coh.magic,        raw + pos + 0, 4);
            memcpy(&coh.headerSize,   raw + pos + 4, 2);
            memcpy(&coh.headerVersion, raw + pos + 6, 2);
            memcpy(&coh.objectSize,   raw + pos + 8, 4);
            memcpy(&coh.objectType,   raw + pos + 12, 4);

            if (coh.magic != OBJ_MAGIC || coh.objectSize == 0 || coh.headerSize < 16)
                break;

            int dataStart = pos + coh.headerSize;
            int dataLen   = static_cast<int>(coh.objectSize) - coh.headerSize;

            if (dataStart + dataLen <= data.size() && dataLen > 0) {
                const char *frameData = raw + dataStart;

                if (coh.objectType == OBJ_CAN_MESSAGE ||
                    coh.objectType == OBJ_CAN_MESSAGE2) {
                    CanFrame frame = parseCanFrame(frameData, dataLen, false);
                    if (frame.dlc > 0 || frame.id > 0) {
                        if (firstFrame) {
                            baseTimestamp = frame.timestamp;
                            firstFrame = false;
                        }
                        frame.timestamp -= baseTimestamp;
                        result.append(frame);
                    }
                } else if (coh.objectType == OBJ_CAN_FD_MESSAGE ||
                           coh.objectType == OBJ_CAN_FD_MESSAGE_64) {
                    CanFrame frame = parseCanFrame(frameData, dataLen, true);
                    if (frame.dlc > 0 || frame.id > 0) {
                        if (firstFrame) {
                            baseTimestamp = frame.timestamp;
                            firstFrame = false;
                        }
                        frame.timestamp -= baseTimestamp;
                        result.append(frame);
                    }
                }
            }

            // 前进到下一个对象
            pos += coh.objectSize;
            // 容器内对象不需要对齐
        }
    }
}

// ============================================================
//  BlfImporter::importFile
// ============================================================
QVector<CanFrame> BlfImporter::importFile(
    const QString &filePath,
    std::function<void(double)> progress)
{
    QVector<CanFrame> result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "BLF: 无法打开文件" << filePath;
        return result;
    }

    const qint64 fileSize = file.size();
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    // ---- 文件头 ----
    // 结构: magic(4) + length(4) + padding(8) + version(4) + objectCount(4) +
    //        fileSize(8) + unknown(8) + compression(4) + rest...
    quint32 magic;
    quint32 headerLength;
    in >> magic >> headerLength;

    if (magic != FILE_MAGIC) {
        qWarning() << "BLF: 无效的文件头 magic";
        return result;
    }

    // 使用文件头中的 length 字段（通常为 144，但不同版本可能不同）
    if (headerLength < 16 || headerLength > 1024) {
        qWarning() << "BLF: 异常的文件头长度" << headerLength << "使用默认 144";
        headerLength = 144;
    }
    file.seek(headerLength);

    bool firstFrame = true;
    double baseTimestamp = 0.0;
    int containerCount = 0;
    int failCount = 0;

    // ---- 遍历顶层对象 ----
    while (!file.atEnd()) {
        const qint64 objPos = file.pos();
        if (objPos + 16 > fileSize) break;

        ObjHeader oh;
        if (!readObjHeader(in, oh)) {
            qWarning() << "BLF: 读取对象头失败 at pos" << objPos;
            break;
        }

        if (oh.objectType == OBJ_LOG_CONTAINER) {
            // ---- Log Container ----
            // 数据区结构 (紧接 headerSize 之后):
            //   uint32 uncompressedSize | uint8 compressionMethod | [compressed data]
            // 注意: LogContainer 结构为 packed 5 字节，无 padding
            file.seek(objPos + oh.headerSize);

            quint32 uncompressedSize;
            quint8  compressionMethod;
            in >> uncompressedSize >> compressionMethod;

            // 压缩数据紧接在 5 字节之后
            const int containerHeaderSize = 5; // uncompressedSize(4) + compressionMethod(1)
            const int compressedSize = static_cast<int>(oh.objectSize)
                                     - static_cast<int>(oh.headerSize)
                                     - containerHeaderSize;

            if (compressedSize <= 0) {
                qint64 next = objPos + oh.objectSize;
                if (next % 4 != 0) next += 4 - (next % 4);
                file.seek(next);
                continue;
            }

            QByteArray compressedData = file.read(compressedSize);
            containerCount++;

            // 解压
            QByteArray containerData = decompressContainer(
                compressedData, uncompressedSize, compressionMethod);

            if (containerData.isEmpty()) {
                failCount++;
                qint64 next = objPos + oh.objectSize;
                if (next % 4 != 0) next += 4 - (next % 4);
                file.seek(next);
                continue;
            }

            // 解析容器内的 CAN 帧对象
            parseCanObjects(containerData, result, firstFrame, baseTimestamp);
        }
        else if (oh.objectType == OBJ_CAN_MESSAGE ||
                 oh.objectType == OBJ_CAN_MESSAGE2 ||
                 oh.objectType == OBJ_CAN_FD_MESSAGE ||
                 oh.objectType == OBJ_CAN_FD_MESSAGE_64) {
            // ---- 顶层 CAN 帧（不常见，但某些文件可能有）----
            file.seek(objPos + oh.headerSize);
            int dataLen = static_cast<int>(oh.objectSize) - static_cast<int>(oh.headerSize);
            QByteArray frameRaw = file.read(dataLen);
            bool isFd = (oh.objectType == OBJ_CAN_FD_MESSAGE ||
                         oh.objectType == OBJ_CAN_FD_MESSAGE_64);
            CanFrame frame = parseCanFrame(frameRaw.constData(), frameRaw.size(), isFd);
            if (frame.dlc > 0 || frame.id > 0) {
                if (firstFrame) {
                    baseTimestamp = frame.timestamp;
                    firstFrame = false;
                }
                frame.timestamp -= baseTimestamp;
                result.append(frame);
            }
        }

        // 前进到下一个顶层对象（4 字节对齐）
        qint64 nextPos = objPos + oh.objectSize;
        if (nextPos % 4 != 0)
            nextPos += 4 - (nextPos % 4);
        file.seek(nextPos);

        if (progress && fileSize > 0)
            progress(static_cast<double>(file.pos()) / fileSize);
    }

    if (progress) progress(1.0);
    qDebug() << "BLF 导入完成:" << result.size() << "帧,"
             << "容器:" << containerCount << "个,"
             << "解压失败:" << failCount << "个";
    return result;
}
