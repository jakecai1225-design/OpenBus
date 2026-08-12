#ifndef RECORDER_H
#define RECORDER_H

#include <QObject>
#include <QFile>
#include <memory>
#include "core/canframe.h"
#include "core/canfileio/canfileio.h"

class CanFileWriter;

/**
 * @brief 报文录制器
 *
 * 将接收到的 CanFrame 流式写入文件，支持 BLF / ASC / CSV 格式。
 * 根据文件扩展名自动选择写入器。
 */
class Recorder : public QObject
{
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder();

    bool start(const QString &filePath);
    void stop();
    bool isRecording() const { return m_recording; }
    void pause() { m_paused = true; }
    void resume() { m_paused = false; }
    bool isPaused() const { return m_paused; }
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
    bool m_paused = false;
    std::unique_ptr<CanFileWriter> m_writer;
    QString m_filePath;
    int m_frameCount = 0;
};

#endif // RECORDER_H
