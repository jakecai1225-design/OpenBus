#ifndef BLF_H
#define BLF_H

#include "canfileio.h"

/// BLF (Binary Logging Format) 写入器 — Vector 二进制格式
class BlfWriter : public CanFileWriter
{
public:
    BlfWriter();
    ~BlfWriter() override;

    bool open(const QString &filePath) override;
    void writeFrame(const CanFrame &frame) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }
    int frameCount() const override { return m_frameCount; }

private:
    QFile m_file;
    qint64 m_fileHeaderPos = 0;   // 文件头位置（用于回写帧数）
    int   m_frameCount = 0;
    qint64 m_startTimeNs = 0;    // 录制起始时间（10ns ticks）

    // LOG container 缓冲
    QByteArray m_containerBuf;
    static constexpr int MAX_CONTAINER_SIZE = 65536; // 64KB

    void flushContainer();
    void writeFileHeader();
    void updateFileHeader();
};

/// BLF (Binary Logging Format) 读取器 — Vector 二进制格式
class BlfReader : public CanFileReader
{
public:
    BlfReader();
    ~BlfReader() override;

    bool open(const QString &filePath) override;
    int readAll(QVector<CanFrame> &frames) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }

private:
    QFile m_file;

    // 读取 LOG container 内的帧
    int parseContainer(const QByteArray &containerData, QVector<CanFrame> &frames);
};

#endif // BLF_H
