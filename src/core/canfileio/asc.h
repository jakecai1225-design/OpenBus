#ifndef ASC_H
#define ASC_H

#include "canfileio.h"
#include <QFile>
#include <QTextStream>

/// ASC (ASCII Logging Format) 写入器 — Vector 文本格式
class AscWriter : public CanFileWriter
{
public:
    AscWriter();
    ~AscWriter() override;

    bool open(const QString &filePath) override;
    void writeFrame(const CanFrame &frame) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }
    int frameCount() const override { return m_frameCount; }

private:
    QFile m_file;
    QTextStream m_stream;
    int m_frameCount = 0;
    double m_baseTime = 0.0;
    bool m_hasBaseTime = false;

    void writeHeader();
};

/// ASC (ASCII Logging Format) 读取器 — Vector 文本格式
class AscReader : public CanFileReader
{
public:
    AscReader();
    ~AscReader() override;

    bool open(const QString &filePath) override;
    int readAll(QVector<CanFrame> &frames) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }

private:
    QFile m_file;
};

#endif // ASC_H
