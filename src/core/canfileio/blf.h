#ifndef BLF_H
#define BLF_H

#include "canfileio.h"

#include <memory>

class QFile;

/// BLF (Binary Logging Format) 写入器 — 基于 third_party/blf 库
class BlfWriter : public CanFileWriter
{
public:
    BlfWriter();
    ~BlfWriter() override;

    bool open(const QString &filePath) override;
    void writeFrame(const CanFrame &frame) override;
    void close() override;
    bool isOpen() const override;
    int frameCount() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// BLF (Binary Logging Format) 读取器 — 基于 third_party/blf 库
class BlfReader : public CanFileReader
{
public:
    BlfReader();
    ~BlfReader() override;

    bool open(const QString &filePath) override;
    int readAll(QVector<CanFrame> &frames) override;
    void close() override;
    bool isOpen() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    int parseUncompressedObjects(const QByteArray &data, QVector<CanFrame> &frames);
};

#endif // BLF_H
