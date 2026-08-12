#include "logsplitter.h"
#include "core/canfileio/canfileio.h"
#include "core/canfileio/canfileio_factory.h"
#include <QDir>
#include <QFileInfo>
#include <QDateTime>

LogSplitter::LogSplitter(QObject *parent)
    : QObject(parent)
{
}

LogSplitter::~LogSplitter()
{
    stop();
}

bool LogSplitter::start(const Config &config)
{
    m_config = config;
    m_currentSeq = 0;
    m_frameCount = 0;
    m_currentSize = 0;

    QDir dir(m_config.directory);
    if (!dir.exists())
        dir.mkpath(".");

    m_recording = openNewFile();
    return m_recording;
}

void LogSplitter::stop()
{
    if (!m_recording) return;
    closeCurrentFile();
    m_recording = false;
}

QString LogSplitter::generateFileName(int seq) const
{
    return QString("%1_%2.%3")
        .arg(m_config.prefix)
        .arg(seq, 3, 10, QChar('0'))
        .arg(m_config.format);
}

bool LogSplitter::openNewFile()
{
    m_currentSeq++;

    m_currentPath = QDir(m_config.directory).filePath(
        QString("%1_%2.%3").arg(m_config.prefix)
            .arg(m_currentSeq, 3, 10, QChar('0')).arg(m_config.format));
    m_currentSize = 0;

    m_writer = CanFileIOFactory::createWriter(m_currentPath);
    if (!m_writer || !m_writer->open(m_currentPath)) {
        m_writer.reset();
        return false;
    }
    return true;
}

void LogSplitter::closeCurrentFile()
{
    if (m_writer) {
        m_writer->close();
        m_writer.reset();
    }
}

bool LogSplitter::shouldSplit(double timestamp) const
{
    if (m_config.splitBySize && m_currentSize >= m_config.maxSizeBytes)
        return true;
    if (m_config.splitByTime && m_fileStartTime > 0 &&
        (timestamp - m_fileStartTime) >= m_config.maxTimeSeconds)
        return true;
    return false;
}

void LogSplitter::pruneOldFiles()
{
    QDir dir(m_config.directory);
    QStringList filters;
    filters << QString("%1_*.%2").arg(m_config.prefix, m_config.format);
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

    while (files.size() > m_config.maxFiles) {
        dir.remove(files.first());
        files.removeFirst();
    }
}

void LogSplitter::recordFrame(const CanFrame &frame)
{
    if (!m_recording || !m_writer) return;

    // 检查分片
    if (shouldSplit(frame.timestamp)) {
        QString oldFile = m_currentPath;
        closeCurrentFile();

        if (m_config.ringMode)
            pruneOldFiles();

        if (!openNewFile()) {
            m_recording = false;
            return;
        }
        emit fileSplit(oldFile, m_currentPath);
    }

    if (m_fileStartTime == 0.0)
        m_fileStartTime = frame.timestamp;

    m_writer->writeFrame(frame);
    m_currentSize += frame.length() + 16;  // 近似估算
    m_frameCount++;
    emit frameRecorded(m_frameCount);
}
