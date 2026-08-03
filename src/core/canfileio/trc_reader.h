#ifndef TRC_READER_H
#define TRC_READER_H

#include "canfileio.h"
#include <QFile>

/// TRC (Trace Format) 读取器 — Vector 旧版文本格式
///
/// 支持 TRC v1.0 和 v2.0 格式，解析通道、ID、DLC、数据等字段。
class TrcReader : public CanFileReader
{
public:
    TrcReader();
    ~TrcReader() override;

    bool open(const QString &filePath) override;
    int readAll(QVector<CanFrame> &frames) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }

private:
    QFile m_file;

    // 解析单行为 CanFrame
    bool parseLine(const QString &line, CanFrame &frame, int seq) const;
};

#endif // TRC_READER_H
