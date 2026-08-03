#include "blf.h"
#include "core/canframe.h"

#include <QDataStream>
#include <QDateTime>
#include <QBuffer>
#include <QtEndian>
#include <cstring>

// ============================================================
//  BLF 格式常量
// ============================================================

namespace {

// 对象魔数 "LOBJ" (little-endian: 0x4A424F4C)
constexpr quint32 BLF_MAGIC = 0x4A424F4C;

// 文件魔数 "LOGG" (little-endian: 0x47474F4C)
constexpr quint32 BLF_FILE_MAGIC = 0x47474F4C;

// 对象类型
constexpr quint32 OBJ_FILE_HEADER     = 0x00000002;
constexpr quint32 OBJ_LOG_CONTAINER   = 0x00000001;
constexpr quint32 OBJ_CAN_MESSAGE     = 0x00000100;
constexpr quint32 OBJ_CAN_FD_MSG_64   = 0x00010302;

// 对象头大小
constexpr quint16 HEADER_SIZE_BASIC   = 16;
constexpr quint16 HEADER_SIZE_WITH_TS = 28;

// LOG container 头部数据区大小（对象头之后的元数据）
constexpr int LOG_CONTAINER_META_SIZE = 8; // uncompressedSize(4) + compression(1) + padding(3)

// 文件头总大小
constexpr int FILE_HEADER_SIZE = 144;

// 将秒转换为 10ns ticks
inline quint64 secondsToBlfTimestamp(double seconds)
{
    return static_cast<quint64>(seconds * 1e8);
}

// 将 10ns ticks 转换为秒
inline double blfTimestampToSeconds(quint64 ticks)
{
    return static_cast<double>(ticks) / 1e8;
}

// 写入对象头（带时间戳版本，headerSize=28）
void writeObjectHeader(QDataStream &ds, quint32 objectType, quint32 objectSize, quint64 timestamp)
{
    ds << BLF_MAGIC;                    // magic
    ds << HEADER_SIZE_WITH_TS;          // headerSize = 28
    ds << static_cast<quint16>(1);      // headerVersion = 1
    ds << objectSize;                   // objectSize (不含 padding)
    ds << objectType;                    // objectType
    // 扩展头：flags(1) + reserved(3) + timestamp(8)
    ds << static_cast<quint8>(0);       // flags
    ds << static_cast<quint8>(0);       // reserved
    ds << static_cast<quint8>(0);       // reserved
    ds << static_cast<quint8>(0);       // reserved
    ds << timestamp;                     // timestamp (10ns ticks)
}

// 写入对象头（基础版本，headerSize=18）
void writeBasicObjectHeader(QDataStream &ds, quint32 objectType, quint32 objectSize)
{
    ds << BLF_MAGIC;                    // magic
    ds << static_cast<quint16>(18);     // headerSize = 18
    ds << static_cast<quint16>(1);      // headerVersion = 1
    ds << objectSize;                   // objectSize
    ds << objectType;                    // objectType
    ds << static_cast<quint16>(0);      // flags (padding to 18 bytes)
}

// 计算对象 padding（对齐到 4 字节）
int objectPadding(quint32 objectSize)
{
    return (4 - (objectSize % 4)) % 4;
}

} // namespace

// ============================================================
//  BlfWriter
// ============================================================

BlfWriter::BlfWriter() = default;

BlfWriter::~BlfWriter()
{
    if (isOpen())
        close();
}

bool BlfWriter::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly))
        return false;

    m_frameCount = 0;
    m_containerBuf.clear();
    m_startTimeNs = QDateTime::currentMSecsSinceEpoch() * 10000ULL; // ms → 10ns ticks

    // 写文件头对象
    m_fileHeaderPos = m_file.pos();
    writeFileHeader();

    return true;
}

void BlfWriter::writeFileHeader()
{
    QDataStream ds(&m_file);
    ds.setByteOrder(QDataStream::LittleEndian);

    // 对象头：headerSize=18
    writeBasicObjectHeader(ds, OBJ_FILE_HEADER, FILE_HEADER_SIZE);

    // 文件头数据区 (144 - 18 = 126 bytes)
    ds << BLF_FILE_MAGIC;                 // file signature
    ds << static_cast<quint32>(0);        // fileLength (回写)
    ds << static_cast<quint32>(0);        // unknown
    ds << static_cast<quint32>(0);        // unknown
    ds << static_cast<quint32>(0);        // objectCount
    ds << static_cast<quint32>(0);        // objectSize
    ds << static_cast<quint8>(0);         // compressionMethod
    ds << static_cast<quint8>(0);         // padding

    // 填充剩余空间
    int remaining = FILE_HEADER_SIZE - 18 - 4*6 - 2;
    for (int i = 0; i < remaining; ++i)
        ds << static_cast<quint8>(0xFF);
}

void BlfWriter::updateFileHeader()
{
    qint64 savedPos = m_file.pos();
    m_file.seek(m_fileHeaderPos);

    QDataStream ds(&m_file);
    ds.setByteOrder(QDataStream::LittleEndian);

    // 重写文件头
    writeFileHeader();
    m_file.seek(savedPos);
}

void BlfWriter::writeFrame(const CanFrame &frame)
{
    if (!isOpen())
        return;

    // 构建帧对象
    QByteArray objData;
    QDataStream ds(&objData, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint64 timestamp = secondsToBlfTimestamp(frame.timestamp);

    // 帧标志位编码
    quint8 flags = 0;
    if (frame.direction == CanFrame::Tx) flags |= 0x01;
    if (frame.extended)                  flags |= 0x04;
    // CAN FD 附加标志
    if (frame.bitrateSwitch)             flags |= 0x08;
    if (frame.errorState)               flags |= 0x10;

    quint16 channel = frame.channel;
    quint32 objectId = frame.id;

    if (frame.fd) {
        // CAN_FD_MESSAGE_64 (type 0x00010302)
        // 数据布局: channel(2) + dlc(1) + flags(1) + id(4) + data(64) = 72 bytes
        // 对象头(28) + 数据(72) = 100 bytes
        quint32 objSize = HEADER_SIZE_WITH_TS + 72;

        writeObjectHeader(ds, OBJ_CAN_FD_MSG_64, objSize, timestamp);
        ds << channel;
        ds << frame.dlc;
        ds << flags;
        ds << objectId;

        // 填充 64 字节数据区
        QByteArray fdData = frame.data;
        fdData.resize(64);
        ds.writeRawData(fdData.constData(), 64);

        // padding
        int pad = objectPadding(objSize);
        for (int i = 0; i < pad; ++i)
            ds << static_cast<quint8>(0);
    } else {
        // CAN_MESSAGE (type 0x00000100)
        // 数据布局: channel(2) + dlc(1) + flags(1) + id(4) + data(8) = 16 bytes
        // 对象头(28) + 数据(16) = 44 bytes
        quint32 objSize = HEADER_SIZE_WITH_TS + 16;

        writeObjectHeader(ds, OBJ_CAN_MESSAGE, objSize, timestamp);
        ds << channel;
        ds << frame.dlc;
        ds << flags;
        ds << objectId;

        // 填充 8 字节数据区
        QByteArray canData = frame.data;
        canData.resize(8);
        ds.writeRawData(canData.constData(), 8);

        // padding
        int pad = objectPadding(objSize);
        for (int i = 0; i < pad; ++i)
            ds << static_cast<quint8>(0);
    }

    // 将帧对象追加到 LOG container 缓冲
    m_containerBuf.append(objData);

    // 缓冲区满则刷出 LOG container
    if (m_containerBuf.size() >= MAX_CONTAINER_SIZE)
        flushContainer();

    ++m_frameCount;
}

void BlfWriter::flushContainer()
{
    if (m_containerBuf.isEmpty())
        return;

    QDataStream ds(&m_file);
    ds.setByteOrder(QDataStream::LittleEndian);

    // LOG container 对象大小 = 头(18) + 元数据(8) + 数据
    quint32 containerSize = 18 + LOG_CONTAINER_META_SIZE + m_containerBuf.size();

    // 对象头 (headerSize=18)
    writeBasicObjectHeader(ds, OBJ_LOG_CONTAINER, containerSize);

    // LOG container 元数据
    ds << static_cast<quint32>(0);   // uncompressedSize (0 = 未压缩)
    ds << static_cast<quint8>(0);    // compressionMethod (0 = 无压缩)
    ds << static_cast<quint8>(0);    // padding
    ds << static_cast<quint8>(0);    // padding
    ds << static_cast<quint8>(0);    // padding

    // 写入帧对象数据
    ds.writeRawData(m_containerBuf.constData(), m_containerBuf.size());

    // container padding
    int pad = objectPadding(containerSize);
    for (int i = 0; i < pad; ++i)
        ds << static_cast<quint8>(0);

    m_containerBuf.clear();
}

void BlfWriter::close()
{
    if (!isOpen())
        return;

    // 刷出剩余帧
    flushContainer();

    // 更新文件头中的帧计数等信息
    updateFileHeader();

    m_file.close();
}

// ============================================================
//  BlfReader
// ============================================================

BlfReader::BlfReader() = default;

BlfReader::~BlfReader()
{
    close();
}

bool BlfReader::open(const QString &filePath)
{
    m_file.setFileName(filePath);
    return m_file.open(QIODevice::ReadOnly);
}

int BlfReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    QDataStream ds(&m_file);
    ds.setByteOrder(QDataStream::LittleEndian);

    int totalFrames = 0;

    while (!ds.atEnd()) {
        // 读取对象头基础部分 (16 bytes)
        quint32 magic, objectSize, objectType;
        quint16 headerSize, headerVersion;

        ds >> magic >> headerSize >> headerVersion >> objectSize >> objectType;
        if (ds.status() != QDataStream::Ok)
            break;

        if (magic != BLF_MAGIC) {
            // 跳过无效对象
            if (objectSize > 16)
                ds.skipRawData(objectSize - 16);
            continue;
        }

        // 读取扩展头
        quint64 timestamp = 0;
        if (headerSize > 16) {
            int extraHeader = headerSize - 16;
            QByteArray headerExtra(extraHeader, Qt::Uninitialized);
            ds.readRawData(headerExtra.data(), extraHeader);

            // 如果 headerSize >= 28，最后 8 字节是 timestamp
            if (headerSize >= 28) {
                const auto *raw = reinterpret_cast<const quint8 *>(headerExtra.constData());
                // timestamp 在扩展头偏移 4 的位置（flags(1)+reserved(3) 之后）
                if (headerSize >= 28) {
                    int tsOffset = headerSize - 16 - 8;
                    if (tsOffset >= 0 && tsOffset + 8 <= headerExtra.size()) {
                        timestamp = qFromLittleEndian<quint64>(raw + tsOffset);
                    }
                }
            }
        }

        // 读取对象数据
        int dataSize = objectSize - headerSize;
        if (dataSize < 0)
            dataSize = 0;

        QByteArray objData(dataSize, Qt::Uninitialized);
        if (dataSize > 0)
            ds.readRawData(objData.data(), dataSize);

        // 跳过对象 padding
        int pad = objectPadding(objectSize);
        if (pad > 0)
            ds.skipRawData(pad);

        // 处理不同对象类型
        if (objectType == OBJ_FILE_HEADER) {
            // 文件头，忽略
        } else if (objectType == OBJ_LOG_CONTAINER) {
            // LOG container — 解析其中的帧对象
            // 跳过 LOG container 元数据 (8 bytes)
            if (objData.size() > LOG_CONTAINER_META_SIZE) {
                QByteArray containerData = objData.mid(LOG_CONTAINER_META_SIZE);
                totalFrames += parseContainer(containerData, frames);
            }
        }
        // 其他顶层对象类型暂不处理
    }

    return totalFrames;
}

int BlfReader::parseContainer(const QByteArray &containerData, QVector<CanFrame> &frames)
{
    QBuffer buf;
    buf.setData(containerData);
    buf.open(QIODevice::ReadOnly);

    QDataStream ds(&buf);
    ds.setByteOrder(QDataStream::LittleEndian);

    int count = 0;

    while (!ds.atEnd()) {
        quint32 magic, objectSize, objectType;
        quint16 headerSize, headerVersion;

        ds >> magic >> headerSize >> headerVersion >> objectSize >> objectType;
        if (ds.status() != QDataStream::Ok)
            break;

        if (magic != BLF_MAGIC) {
            if (objectSize > 16)
                ds.skipRawData(objectSize - 16);
            continue;
        }

        // 读取扩展头
        quint64 timestamp = 0;
        if (headerSize > 16) {
            int extraHeader = headerSize - 16;
            QByteArray headerExtra(extraHeader, Qt::Uninitialized);
            ds.readRawData(headerExtra.data(), extraHeader);

            if (headerSize >= 28) {
                int tsOffset = headerSize - 16 - 8;
                if (tsOffset >= 0 && tsOffset + 8 <= headerExtra.size()) {
                    const auto *raw = reinterpret_cast<const quint8 *>(headerExtra.constData());
                    timestamp = qFromLittleEndian<quint64>(raw + tsOffset);
                }
            }
        }

        // 读取对象数据
        int dataSize = objectSize - headerSize;
        if (dataSize < 0)
            dataSize = 0;

        QByteArray objData(dataSize, Qt::Uninitialized);
        if (dataSize > 0)
            ds.readRawData(objData.data(), dataSize);

        // 跳过 padding
        int pad = objectPadding(objectSize);
        if (pad > 0)
            ds.skipRawData(pad);

        // 解析帧对象
        CanFrame frame;
        bool isFrame = false;

        if (objectType == OBJ_CAN_MESSAGE && objData.size() >= 16) {
            // CAN_MESSAGE: channel(2) + dlc(1) + flags(1) + id(4) + data(8)
            const auto *p = reinterpret_cast<const quint8 *>(objData.constData());
            frame.channel = qFromLittleEndian<quint16>(p);
            frame.dlc = p[2];
            quint8 flags = p[3];
            frame.id = qFromLittleEndian<quint32>(p + 4);
            frame.extended = (flags & 0x04) != 0;
            frame.direction = (flags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
            frame.fd = false;
            frame.bitrateSwitch = false;
            frame.errorState = false;

            int dataLen = qMin(static_cast<int>(frame.dlc), 8);
            frame.data = QByteArray(reinterpret_cast<const char *>(p + 8), dataLen);
            isFrame = true;
        } else if (objectType == OBJ_CAN_FD_MSG_64 && objData.size() >= 72) {
            // CAN_FD_MESSAGE_64: channel(2) + dlc(1) + flags(1) + id(4) + data(64)
            const auto *p = reinterpret_cast<const quint8 *>(objData.constData());
            frame.channel = qFromLittleEndian<quint16>(p);
            frame.dlc = p[2];
            quint8 flags = p[3];
            frame.id = qFromLittleEndian<quint32>(p + 4);
            frame.extended = (flags & 0x04) != 0;
            frame.direction = (flags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
            frame.fd = true;
            frame.bitrateSwitch = (flags & 0x08) != 0;
            frame.errorState = (flags & 0x10) != 0;

            int dataLen = CanFrame::dlcToLength(frame.dlc);
            dataLen = qMin(dataLen, 64);
            frame.data = QByteArray(reinterpret_cast<const char *>(p + 8), dataLen);
            isFrame = true;
        }

        if (isFrame) {
            frame.timestamp = blfTimestampToSeconds(timestamp);
            frames.append(frame);
            ++count;
        }
    }

    return count;
}

void BlfReader::close()
{
    m_file.close();
}
