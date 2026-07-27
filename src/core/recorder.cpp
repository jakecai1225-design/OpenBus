#include "recorder.h"

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

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly))
        return false;

    m_stream.setDevice(&m_file);
    m_stream.setVersion(QDataStream::Qt_6_0);
    m_stream.setByteOrder(QDataStream::LittleEndian);

    // 写文件头
    m_stream << MAGIC;
    m_stream << VERSION;

    // 记录帧计数位置，停止时回写
    m_frameCountPos = m_file.pos();
    quint32 count = 0;
    m_stream << count;

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

    // 回写帧计数
    qint64 savedPos = m_file.pos();
    m_file.seek(m_frameCountPos);
    m_stream << static_cast<quint32>(m_frameCount);
    m_file.seek(savedPos);

    m_file.close();
    m_recording = false;

    emit recordingStopped(m_filePath, m_frameCount);
    m_filePath.clear();
}

void Recorder::recordFrame(const CanFrame &frame)
{
    if (!m_recording)
        return;

    m_stream << frame;
    ++m_frameCount;
    emit frameRecorded(m_frameCount);
}
