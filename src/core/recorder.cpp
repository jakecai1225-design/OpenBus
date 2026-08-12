#include "recorder.h"
#include "core/canfileio/canfileio_factory.h"

#include <QFileInfo>

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
}

Recorder::~Recorder()
{
    if (m_recording)
        stop();
}

bool Recorder::start(const QString &filePath)
{
    if (m_recording)
        stop();

    // 根据扩展名创建写入器
    m_writer = CanFileIOFactory::createWriter(filePath);
    if (!m_writer) {
        // 不支持的格式
        return false;
    }

    if (!m_writer->open(filePath))
        return false;

    m_recording = true;
    m_frameCount = 0;
    m_filePath = filePath;
    emit recordingStarted(filePath);
    return true;
}

void Recorder::stop()
{
    if (!m_recording)
        return;

    m_writer->close();
    m_writer.reset();

    m_recording = false;
    m_paused = false;
    emit recordingStopped(m_filePath, m_frameCount);
    m_filePath.clear();
}

void Recorder::recordFrame(const CanFrame &frame)
{
    if (!m_recording || !m_writer || m_paused)
        return;

    m_writer->writeFrame(frame);
    ++m_frameCount;
    emit frameRecorded(m_frameCount);
}
