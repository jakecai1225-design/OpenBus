#include "blf.h"
#include "core/canframe.h"

#include <Vector/BLF/File.h>
#include <Vector/BLF/CanMessage.h>
#include <Vector/BLF/CanMessage2.h>
#include <Vector/BLF/CanFdMessage.h>
#include <Vector/BLF/CanFdMessage64.h>
#include <Vector/BLF/ObjectHeader.h>

#include <memory>
#include <iostream>

// ============================================================
//  BlfWriter — 委托 Vector::BLF 库
// ============================================================

struct BlfWriter::Impl
{
    std::unique_ptr<Vector::BLF::File> file;
    int frameCount = 0;
    double baseTime = 0.0;
    bool hasBaseTime = false;
};

BlfWriter::BlfWriter()
    : m_impl(std::make_unique<Impl>())
{
}

BlfWriter::~BlfWriter()
{
    close();
}

bool BlfWriter::open(const QString &filePath)
{
    m_impl->file = std::make_unique<Vector::BLF::File>();
    m_impl->file->compressionLevel = 1; // 最快压缩
    try {
        m_impl->file->open(filePath.toStdString(), std::ios_base::out);
    } catch (const std::exception &e) {
        std::cerr << "BLF writer open failed: " << e.what() << std::endl;
        m_impl->file.reset();
        return false;
    }
    m_impl->frameCount = 0;
    m_impl->hasBaseTime = false;
    return m_impl->file->is_open();
}

void BlfWriter::writeFrame(const CanFrame &frame)
{
    if (!isOpen())
        return;

    double ts = frame.timestamp;
    if (!m_impl->hasBaseTime) {
        m_impl->baseTime = ts;
        m_impl->hasBaseTime = true;
    }
    // 转为 BLF 纳秒时间戳（绝对时间，从 baseTime 开始）
    uint64_t objectTimeStamp = static_cast<uint64_t>((ts - m_impl->baseTime) * 1e9);

    if (frame.fd) {
        // 使用 CanFdMessage64 (type 101)
        auto *msg = new Vector::BLF::CanFdMessage64();
        msg->objectFlags = Vector::BLF::ObjectHeader::TimeOneNans;
        msg->objectTimeStamp = objectTimeStamp;
        msg->channel = frame.channel;
        msg->dlc = frame.dlc;
        msg->validDataBytes = static_cast<uint8_t>(frame.data.size());
        msg->id = frame.id;

        // CanFdMessage64 flags:
        // Bit 6 (0x40): TX, Bit 12 (0x1000): EDL, Bit 13 (0x2000): BRS, Bit 14 (0x4000): ESI
        uint32_t flags = 0x1000; // EDL = 1 (CAN FD)
        if (frame.direction == CanFrame::Tx) flags |= 0x40;
        if (frame.bitrateSwitch)             flags |= 0x2000;
        if (frame.errorState)                flags |= 0x4000;
        msg->flags = flags;

        msg->data.assign(frame.data.constData(),
                         frame.data.constData() + frame.data.size());

        m_impl->file->write(msg);
    } else {
        // 经典 CAN — 使用 CanFdMessage64 但 EDL=0
        auto *msg = new Vector::BLF::CanFdMessage64();
        msg->objectFlags = Vector::BLF::ObjectHeader::TimeOneNans;
        msg->objectTimeStamp = objectTimeStamp;
        msg->channel = frame.channel;
        msg->dlc = frame.dlc;
        msg->validDataBytes = static_cast<uint8_t>(frame.data.size());
        msg->id = frame.id;

        uint32_t flags = 0; // EDL = 0 (classic CAN)
        if (frame.direction == CanFrame::Tx) flags |= 0x40;
        msg->flags = flags;

        msg->data.assign(frame.data.constData(),
                         frame.data.constData() + frame.data.size());

        m_impl->file->write(msg);
    }

    ++m_impl->frameCount;
}

void BlfWriter::close()
{
    if (m_impl->file) {
        try {
            m_impl->file->close();
        } catch (...) {}
        m_impl->file.reset();
    }
}

bool BlfWriter::isOpen() const
{
    return m_impl && m_impl->file && m_impl->file->is_open();
}

int BlfWriter::frameCount() const
{
    return m_impl ? m_impl->frameCount : 0;
}

// ============================================================
//  BlfReader — 委托 Vector::BLF 库
// ============================================================

struct BlfReader::Impl
{
    std::unique_ptr<Vector::BLF::File> file;
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
    m_impl->file = std::make_unique<Vector::BLF::File>();
    try {
        m_impl->file->open(filePath.toStdString(), std::ios_base::in);
    } catch (const std::exception &e) {
        std::cerr << "BLF reader open failed: " << e.what() << std::endl;
        m_impl->file.reset();
        return false;
    }
    m_impl->hasBaseTime = false;
    return m_impl->file->is_open();
}

int BlfReader::readAll(QVector<CanFrame> &frames)
{
    if (!isOpen())
        return -1;

    int totalFrames = 0;
    int objCount = 0;

    while (true) {
        Vector::BLF::ObjectHeaderBase *obj = nullptr;
        try {
            obj = m_impl->file->read();
        } catch (const std::exception &e) {
            std::cerr << "BLF read error at obj " << objCount << ": " << e.what() << std::endl;
            break;
        }

        if (!obj)
            break; // EOF
        ++objCount;

        // 提取时间戳（ObjectHeader 层）
        double timestamp = 0.0;
        auto *hdr = dynamic_cast<Vector::BLF::ObjectHeader *>(obj);
        if (hdr) {
            if (hdr->objectFlags & Vector::BLF::ObjectHeader::TimeOneNans) {
                timestamp = static_cast<double>(hdr->objectTimeStamp) / 1e9;
            } else {
                // TimeTenMics: 10 微秒单位
                timestamp = static_cast<double>(hdr->objectTimeStamp) * 1e-5;
            }
        }

        bool frameValid = false;
        CanFrame frame;

        switch (obj->objectType) {
        case Vector::BLF::ObjectType::CAN_MESSAGE: {
            // CanMessage (type 1, deprecated, data[8])
            auto *msg = static_cast<Vector::BLF::CanMessage *>(obj);
            frame.channel = static_cast<quint8>(msg->channel);
            frame.id = msg->id;
            frame.dlc = msg->dlc;
            frame.extended = false; // CanMessage 不区分扩展帧
            frame.direction = (msg->flags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
            int dataLen = qMin(static_cast<int>(msg->dlc), 8);
            frame.data = QByteArray(reinterpret_cast<const char *>(msg->data.data()), dataLen);
            frameValid = true;
            break;
        }
        case Vector::BLF::ObjectType::CAN_MESSAGE2: {
            // CanMessage2 (type 86, data vector)
            auto *msg = static_cast<Vector::BLF::CanMessage2 *>(obj);
            frame.channel = static_cast<quint8>(msg->channel);
            frame.id = msg->id;
            frame.dlc = msg->dlc;
            frame.extended = (msg->id > 0x7FF);
            frame.direction = (msg->flags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
            int dataLen = qMin(static_cast<int>(msg->dlc),
                              static_cast<int>(msg->data.size()));
            frame.data = QByteArray(reinterpret_cast<const char *>(msg->data.data()), dataLen);
            frameValid = true;
            break;
        }
        case Vector::BLF::ObjectType::CAN_FD_MESSAGE: {
            // CanFdMessage (type 100, data[64])
            auto *msg = static_cast<Vector::BLF::CanFdMessage *>(obj);
            frame.channel = static_cast<quint8>(msg->channel);
            frame.id = msg->id;
            frame.dlc = msg->dlc;
            frame.fd = (msg->canFdFlags & Vector::BLF::CanFdMessage::EDL) != 0;
            frame.bitrateSwitch = (msg->canFdFlags & Vector::BLF::CanFdMessage::BRS) != 0;
            frame.errorState = (msg->canFdFlags & Vector::BLF::CanFdMessage::ESI) != 0;
            frame.direction = (msg->flags & 0x01) ? CanFrame::Tx : CanFrame::Rx;
            int dataLen = msg->validDataBytes > 0
                              ? qMin(static_cast<int>(msg->validDataBytes), 64)
                              : CanFrame::dlcToLength(msg->dlc);
            frame.data = QByteArray(reinterpret_cast<const char *>(msg->data.data()), dataLen);
            frameValid = true;
            break;
        }
        case Vector::BLF::ObjectType::CAN_FD_MESSAGE_64: {
            // CanFdMessage64 (type 101, data vector)
            auto *msg = static_cast<Vector::BLF::CanFdMessage64 *>(obj);
            frame.channel = msg->channel;
            frame.id = msg->id;
            frame.dlc = msg->dlc;
            frame.fd = (msg->flags & 0x1000) != 0; // EDL
            frame.bitrateSwitch = (msg->flags & 0x2000) != 0; // BRS
            frame.errorState = (msg->flags & 0x4000) != 0; // ESI
            frame.direction = (msg->flags & 0x40) != 0 ? CanFrame::Tx : CanFrame::Rx; // TX
            int dataLen = msg->validDataBytes > 0
                              ? qMin(static_cast<int>(msg->validDataBytes), 64)
                              : CanFrame::dlcToLength(msg->dlc);
            dataLen = qMin(dataLen, static_cast<int>(msg->data.size()));
            frame.data = QByteArray(reinterpret_cast<const char *>(msg->data.data()), dataLen);
            frameValid = true;
            break;
        }
        default:
            break; // 忽略非 CAN 对象
        }

        delete obj;

        if (frameValid) {
            if (!m_impl->hasBaseTime) {
                m_impl->baseTime = timestamp;
                m_impl->hasBaseTime = true;
            }
            frame.timestamp = timestamp - m_impl->baseTime;
            frames.append(frame);
            ++totalFrames;
        }
    }

    return totalFrames;
}

void BlfReader::close()
{
    if (m_impl->file) {
        try {
            m_impl->file->close();
        } catch (...) {}
        m_impl->file.reset();
    }
}

bool BlfReader::isOpen() const
{
    return m_impl && m_impl->file && m_impl->file->is_open();
}
