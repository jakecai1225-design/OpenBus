#ifndef CSV_H
#define CSV_H

#include "canfileio.h"
#include <QFile>
#include <QTextStream>

/// CSV (Comma-Separated Values) 写入器
class CsvWriter : public CanFileWriter
{
public:
    CsvWriter();
    ~CsvWriter() override;

    bool open(const QString &filePath) override;
    void writeFrame(const CanFrame &frame) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }
    int frameCount() const override { return m_frameCount; }

private:
    QFile m_file;
    QTextStream m_stream;
    int m_frameCount = 0;
};

/// CSV (Comma-Separated Values) 读取器
class CsvReader : public CanFileReader
{
public:
    CsvReader();
    ~CsvReader() override;

    bool open(const QString &filePath) override;
    int readAll(QVector<CanFrame> &frames) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }

private:
    QFile m_file;
};

#endif // CSV_H
