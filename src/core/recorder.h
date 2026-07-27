#ifndef RECORDER_H
#define RECORDER_H

#include <QObject>
#include <QFile>
#include <QDataStream>
#include "core/canframe.h"

/**
 * @brief 报文录制器
 *
 * 将接收到的 CanFrame 以二进制格式写入 .sin 文件。
 * 文件格式：magic(4B) + version(2B) + frameCount(4B) + frames...
 */
class Recorder : public QObject
{
    Q_OBJECT

public:
    static constexpr quint32 MAGIC = 0x53494E31; // "SIN1"
    static constexpr quint16 VERSION = 1;

    explicit Recorder(QObject *parent = nullptr);
    ~Recorder();

    bool start(const QString &filePath);
    void stop();
    bool isRecording() const { return m_recording; }
    QString currentFile() const { return m_filePath; }
    int frameCount() const { return m_frameCount; }

public slots:
    void recordFrame(const CanFrame &frame);

signals:
    void frameRecorded(int totalFrames);
    void recordingStarted(const QString &filePath);
    void recordingStopped(const QString &filePath, int totalFrames);

private:
    bool m_recording = false;
    QFile m_file;
    QDataStream m_stream;
    QString m_filePath;
    int m_frameCount = 0;
    qint64 m_frameCountPos = 0; // 文件中帧计数字段的偏移
};

#endif // RECORDER_H
