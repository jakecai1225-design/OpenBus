#include "blf.h"
#include "core/canframe.h"

#include <QFile>
#include <QByteArray>
#include <QDebug>
#include <QtEndian>
#include <zlib.h>

// ============================================================
//  BLF 格式常量
// ============================================================

static constexpr quint32 BLF_MAGIC_FILE   = 0x47474f4c;  // "LOGG"
static constexpr quint32 BLF_MAGIC_OBJECT = 0x4a424f4c;  // "LOBJ"
static constexpr quint32 BLF_OBJTYPE_LOGCONTAINER = 10;  // BL_OBJ_CONTAINER
static constexpr quint32 BLF_OBJTYPE_CAN_MESSAGE  = 1;   // CanMessage (deprecated)
static constexpr quint32 BLF_OBJTYPE_CAN_MESSAGE2 = 86;  // CanMessage2
static constexpr quint32 BLF_OBJTYPE_CAN_FD       = 100; // CanFdMessage
static constexpr quint32 BLF_OBJTYPE_CAN_FD64     = 101; // CanFdMessage64

// BLF 文件头由 headerLength 字段决定（通常 144 字节）
static constexpr int BLF_FILE_HEADER_SIZE = 144;

// LogContainer 数据区开销：compressionMethod(4) + reserved(4) + uncompressedSize(4) + reserved(4) = 16 字节
static constexpr int LOGCONTAINER_OVERHEAD = 16;

// ============================================================
//  辅助函数
// ============================================================

/// BLF 对象头基础部分为 16 字节（objectType 是 u32）
struct BlfObjectHeader {
    quint32 signature;      // offset 0,  4 bytes
    quint16 headerSize;     // offset 4,  2 bytes
    quint16 headerVersion;  // offset 6,  2 bytes
    quint32 objectSize;      // offset 8,  4 bytes
    quint32 objectType;      // offset 12, 4 bytes
};

static bool readObjectHeader(QDataStream &ds, BlfObjectHeader &hdr)
{
    ds >> hdr.signature;
    ds >> hdr.headerSize;
    ds >> hdr.headerVersion;
    ds >> hdr.objectSize;
    ds >> hdr.objectType;
    return ds.status() == QDataStream::Ok;
}

/// zlib 解压 — BLF 文件通常使用 raw deflate（无 zlib 头）
static QByteArray zlibInflate(const QByteArray &compressed, quint32 expectedSize)
{
    if (expectedSize == 0)
        return {};

    QByteArray output(expectedSize, '\0');

    // 方式1: raw deflate（windowBits = -15），BLF 标准压缩方式
    {
        z_stream strm = {};
        strm.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
        strm.avail_in  = static_cast<uInt>(compressed.size());
        strm.next_out  = reinterpret_cast<Bytef *>(output.data());
        strm.avail_out = static_cast<uInt>(expectedSize);

        if (inflateInit2(&strm, -15) == Z_OK) {
            int ret = inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
            if (ret == Z_STREAM_END) {
                qDebug() << "BLF: raw deflate 解压成功," << compressed.size()
                         << "->" << expectedSize;
                return output;
            }
        }
    }

    // 方式2: zlib-wrapped deflate（带 2 字节头）
    {
        z_stream strm = {};
        strm.next_in   = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
        strm.avail_in  = static_cast<uInt>(compressed.size());
        strm.next_out  = reinterpret_cast<Bytef *>(output.data());
        strm.avail_out = static_cast<uInt>(expectedSize);

        if (inflateInit(&strm) == Z_OK) {
            int ret = inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
            if (ret == Z_STREAM_END || ret == Z_OK) {
                qDebug() << "BLF: zlib-wrapped 解压成功," << compressed.size()
                         << "->" << expectedSize;
                return output;
            }
        }
    }

    qWarning() << "BLF: zlib 解压失败, compressed" << compressed.size()
               << "expected" << expectedSize;
    return {};
}

// ============================================================
//  BlfWriter — 暂不支持（返回 false）
//  BLF 写入需要完整的 BLF 文件结构构造，后续版本实现
// ============================================================

struct BlfWriter::Impl {
    int frameCount = 0;
};

BlfWriter::BlfWriter()
    : m_impl(std::make_unique<Impl>())
{
}

BlfWriter::~BlfWriter()
{
    close();
}

bool BlfWriter::open(const QString &/*filePath*/)
{
    qWarning() << "BLF writer: 暂不支持 BLF 写入，请使用 ASC 或 CSV 格式";
    return false;
}

void BlfWriter::writeFrame(const CanFrame &/*frame*/)
{
}

void BlfWriter::close()
{
}

bool BlfWriter::isOpen() const
{
    return false;
}

int BlfWriter::frameCount() const
{
    return m_impl ? m_impl->frameCount : 0;
}

// ============================================================
//  BlfReader — 手动解析 BLF 文件
// ============================================================

struct BlfReader::Impl {
    QFile file;
    double baseTime = 0.0;
    bool hasBaseTime = false;
};

BlfReader::BlfReader()
    : m_impl(std::make_unique<Impl>())
{
}

BlfReader::~BlfReader()
{
    close();
}

bool BlfReader::open(const QString &filePath)
{
    m_impl->file.setFileName(filePath);
    if (!m_impl->file.open(QIODevice::ReadOnly)) {
        qWarning() << "BLF: 无法打开文件" << filePath;
        return false;
    }

    // 读取前 8 字节: magic(4) + headerLength(4)
    QByteArray headerStart = m_impl->file.read(8);
    if (headerStart.size() < 8) {
        qWarning() << "BLF: 文件头过短 (" << headerStart.size() << "bytes)";
        m_impl->file.close();
        return false;
    }

    // 验证 "LOGG" 魔数
    quint32 magic = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(headerStart.constData()));
    if (magic != BLF_MAGIC_FILE) {
        qWarning() << "BLF: 文件魔数错误:" << Qt::hex << magic << "期望" << BLF_MAGIC_FILE;
        m_impl->file.close();
        return false;
    }

    // 从文件头读取实际 headerLength (偏移 4)
    quint32 headerLength = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(headerStart.constData() + 4));
    if (headerLength < 8 || headerLength > 4096) {
        qWarning() << "BLF: 文件头长度异常:" << headerLength;
        m_impl->file.close();
        return false;
    }

    // 定位到文件头之后（第一个对象的位置）
    m_impl->file.seek(headerLength);

    m_impl->hasBaseTime = false;
    qDebug() << "BLF: 文件打开成功, headerLength=" << headerLength;
    return true;
}

int BlfReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    int totalFrames = 0;
    int containerCount = 0;

    while (!m_impl->file.atEnd()) {
        // 读取对象头基础部分（16 字节）
        QByteArray hdrData = m_impl->file.read(16);
        if (hdrData.size() < 16)
            break;

        BlfObjectHeader hdr;
        QDataStream ds(hdrData);
        ds.setByteOrder(QDataStream::LittleEndian);
        readObjectHeader(ds, hdr);

        if (hdr.signature != BLF_MAGIC_OBJECT) {
            qWarning() << "BLF: 对象魔数错误, 期望 LOBJ, got" << Qt::hex << hdr.signature
                       << "at pos" << m_impl->file.pos() - 16;
            break;
        }

        if (containerCount == 0) {
            qDebug() << "BLF: 第一个对象: type=" << hdr.objectType
                     << "headerSize=" << hdr.headerSize
                     << "objectSize=" << hdr.objectSize
                     << "version=" << hdr.headerVersion;
        }

        // 读取剩余头部 (headerSize - 16 字节)
        int extraHeaderSize = hdr.headerSize - 16;
        QByteArray extraHeader;
        if (extraHeaderSize > 0) {
            extraHeader = m_impl->file.read(extraHeaderSize);
            if (extraHeader.size() < extraHeaderSize)
                break;
        }

        // 计算对象数据大小
        int dataRemaining = static_cast<int>(hdr.objectSize) - hdr.headerSize;

        if (hdr.objectType == BLF_OBJTYPE_LOGCONTAINER) {
            // LogContainer: 读取 16 字节开销 + 压缩数据
            // 结构: u32 compressionMethod + u32 reserved + u32 uncompressedSize + u32 reserved
            if (dataRemaining < LOGCONTAINER_OVERHEAD)
                break;

            QByteArray overheadData = m_impl->file.read(LOGCONTAINER_OVERHEAD);
            if (overheadData.size() < LOGCONTAINER_OVERHEAD)
                break;

            quint32 compressionMethod = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(overheadData.constData()));
            quint32 uncompressedSize = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(overheadData.constData() + 8));

            int compressedSize = dataRemaining - LOGCONTAINER_OVERHEAD;
            QByteArray compressedData = m_impl->file.read(compressedSize);
            if (compressedData.size() < compressedSize)
                break;

            ++containerCount;

            // 解压
            QByteArray uncompressed;
            if (compressionMethod == 0) {
                uncompressed = compressedData;
            } else if (compressionMethod == 2) {
                // zlib deflate
                uncompressed = zlibInflate(compressedData, uncompressedSize);
                if (uncompressed.isEmpty() && uncompressedSize > 0) {
                    qWarning() << "BLF: 解压失败, container" << containerCount
                               << "compressed" << compressedSize
                               << "expected" << uncompressedSize;
                    continue;
                }
            } else {
                qWarning() << "BLF: 不支持的压缩方法" << compressionMethod;
                continue;
            }

            // 解析解压后的对象
            int parsed = parseUncompressedObjects(uncompressed, frames);
            totalFrames += parsed;
            qDebug() << "BLF: container" << containerCount
                     << "parsed" << parsed << "frames"
                     << "(comp" << compressedSize
                     << "-> uncomp" << uncompressed.size() << ")";

        } else {
            // 非 LogContainer 对象，跳过
            m_impl->file.skip(dataRemaining);
        }

        // BLF 对象按 objectSize%4 填充对齐
        int padding = hdr.objectSize % 4;
        if (padding > 0)
            m_impl->file.skip(padding);
    }

    qDebug() << "BLF: 读取完成, containers=" << containerCount
             << "frames=" << totalFrames;
    return totalFrames;
}

int BlfReader::parseUncompressedObjects(const QByteArray &data, QVector<CanFrame> &frames)
{
    int totalFrames = 0;
    int offset = 0;
    const char *raw = data.constData();
    int dataSize = data.size();

    while (offset + 16 <= dataSize) {
        // 读取对象头（基础部分 16 字节）
        quint32 signature = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(raw + offset));
        if (signature != BLF_MAGIC_OBJECT)
            break;

        quint16 headerSize = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar *>(raw + offset + 4));
        quint16 headerVersion = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar *>(raw + offset + 6));
        quint32 objectSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(raw + offset + 8));
        quint32 objectType = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(raw + offset + 12));

        if (objectSize == 0 || offset + objectSize > dataSize)
            break;

        int objDataStart = offset + headerSize;
        int objDataLen = static_cast<int>(objectSize) - headerSize;
        if (objDataStart + objDataLen > dataSize)
            break;

        const char *objData = raw + objDataStart;

        // 提取时间戳 (headerVersion >= 1 时扩展头中有 objectTimeStamp)
        double timestamp = 0.0;
        if (headerVersion >= 1 && headerSize >= 22) {
            // 基础头 16 字节之后: offset+16: u8 objectFlags, offset+17: u8 reserved, offset+18: u32 objectTimeStamp
            quint32 ts = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(raw + offset + 18));
            // 检查 TimeOneNans 或 TimeTenMics 标志
            quint8 flags = static_cast<quint8>(raw[offset + 16]);
            if (flags & 0x02) { // TimeOneNans
                timestamp = static_cast<double>(ts) / 1e9;
            } else { // TimeTenMics
                timestamp = static_cast<double>(ts) * 1e-5;
            }
        }

        CanFrame frame;
        bool frameValid = false;

        switch (objectType) {
        case BLF_OBJTYPE_CAN_MESSAGE: {
            // CanMessage (type 1, deprecated, data[8])
            if (objDataLen >= 12) {
                frame.channel = static_cast<quint8>(qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData)));
                // offset+2: u16 dlc
                frame.dlc = static_cast<quint8>(qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData + 2)));
                // offset+6: u32 id
                frame.id = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(objData + 6));
                // offset+10: u8 flags (bit 0 = TX/RX)
                // offset+11: u8 data[8] or somewhere
                // The CanMessage structure has a fixed data[8] at offset 12
                int dataLen = qMin(static_cast<int>(frame.dlc), 8);
                if (objDataLen >= 12 + dataLen) {
                    frame.data = QByteArray(objData + 12, dataLen);
                    frameValid = true;
                }
            }
            break;
        }
        case BLF_OBJTYPE_CAN_MESSAGE2: {
            // CanMessage2 (type 86, variable data)
            if (objDataLen >= 12) {
                frame.channel = static_cast<quint8>(qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData)));
                frame.dlc = static_cast<quint8>(qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData + 2)));
                // offset+4: u16 flags
                quint16 msgFlags = qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData + 4));
                frame.extended = (msgFlags & 0x08) != 0;
                frame.direction = (msgFlags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
                // offset+6: u32 id
                frame.id = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(objData + 6));
                // offset+10: u8 data[]
                int dataLen = qMin(static_cast<int>(frame.dlc), objDataLen - 12);
                if (dataLen > 0) {
                    frame.data = QByteArray(objData + 12, dataLen);
                }
                frameValid = true;
            }
            break;
        }
        case BLF_OBJTYPE_CAN_FD: {
            // CanFdMessage (type 100, fixed data[64])
            if (objDataLen >= 14) {
                frame.channel = static_cast<quint8>(qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData)));
                // offset+2: u8 dlc
                frame.dlc = static_cast<quint8>(objData[2]);
                // offset+3: u8 validDataBytes
                quint8 validBytes = static_cast<quint8>(objData[3]);
                // offset+4: u32 id
                frame.id = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(objData + 4));
                // offset+8: u8 flags (bit 0 = TX/RX)
                quint8 msgFlags = static_cast<quint8>(objData[8]);
                frame.direction = (msgFlags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
                // offset+10: u16 canFdFlags (bit 4 = EDL, bit 5 = BRS, bit 6 = ESI)
                // Wait, CanFdMessage structure has different layout
                // Let's use: offset+10: u8 canFdFlags
                quint8 fdFlags = static_cast<quint8>(objData[10]);
                frame.fd = (fdFlags & 0x10) != 0;
                frame.bitrateSwitch = (fdFlags & 0x20) != 0;
                frame.errorState = (fdFlags & 0x40) != 0;

                int dataLen = validBytes > 0 ? qMin(static_cast<int>(validBytes), 64)
                                             : CanFrame::dlcToLength(frame.dlc);
                dataLen = qMin(dataLen, qMin(64, objDataLen - 14));
                if (dataLen > 0) {
                    frame.data = QByteArray(objData + 14, dataLen);
                }
                frameValid = true;
            }
            break;
        }
        case BLF_OBJTYPE_CAN_FD64: {
            // CanFdMessage64 (type 101, variable data)
            if (objDataLen >= 12) {
                frame.channel = static_cast<quint8>(qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData)));
                // offset+2: u8 dlc
                frame.dlc = static_cast<quint8>(objData[2]);
                // offset+3: u8 validDataBytes
                quint8 validBytes = static_cast<quint8>(objData[3]);
                // offset+4: u32 id
                frame.id = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(objData + 4));
                // offset+8: u16 flags (bit 6 = TX, bit 12 = EDL, bit 13 = BRS, bit 14 = ESI)
                quint16 msgFlags = qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(objData + 8));
                frame.direction = (msgFlags & 0x40) ? CanFrame::Tx : CanFrame::Rx;
                frame.fd = (msgFlags & 0x1000) != 0;
                frame.bitrateSwitch = (msgFlags & 0x2000) != 0;
                frame.errorState = (msgFlags & 0x4000) != 0;
                frame.extended = (frame.id & 0x80000000) != 0;
                frame.id &= 0x7FFFFFFF;

                int dataLen = validBytes > 0 ? qMin(static_cast<int>(validBytes), 64)
                                             : CanFrame::dlcToLength(frame.dlc);
                dataLen = qMin(dataLen, objDataLen - 12);
                if (dataLen > 0) {
                    frame.data = QByteArray(objData + 12, dataLen);
                }
                frameValid = true;
            }
            break;
        }
        default:
            break; // 忽略非 CAN 对象
        }

        if (frameValid) {
            if (!m_impl->hasBaseTime) {
                m_impl->baseTime = timestamp;
                m_impl->hasBaseTime = true;
            }
            frame.timestamp = timestamp - m_impl->baseTime;
            frames.append(frame);
            ++totalFrames;
        }

        offset += objectSize;
        // BLF 内部对象同样按 objectSize%4 对齐
        offset += objectSize % 4;
    }

    return totalFrames;
}

void BlfReader::close()
{
    if (m_impl->file.isOpen())
        m_impl->file.close();
}

bool BlfReader::isOpen() const
{
    return m_impl && m_impl->file.isOpen();
}
