#include "player.h"
#include "recorder.h"

#include <QFile>
#include <QDataStream>
#include <QDateTime>
#include <algorithm>

Player::Player(QObject *parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(1); // 1ms 精度
    connect(&m_timer, &QTimer::timeout, this, &Player::onTick);
}

bool Player::load(const QString &filePath)
{
    stop();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);
    in.setByteOrder(QDataStream::LittleEndian);

    quint32 magic;
    quint16 version;
    quint32 count;
    in >> magic >> version >> count;

    if (magic != Recorder::MAGIC || version != Recorder::VERSION)
        return false;

    m_frames.clear();
    m_frames.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        CanFrame frame;
        in >> frame;
        if (in.status() != QDataStream::Ok)
            break;
        m_frames.append(frame);
    }

    m_currentIndex = 0;
    emitProgress();
    return true;
}

void Player::loadFrames(const QVector<CanFrame> &frames)
{
    stop();
    m_frames = frames;
    m_currentIndex = 0;
    emitProgress();
}

void Player::unload()
{
    stop();
    m_frames.clear();
    m_currentIndex = 0;
    emitProgress();
}

double Player::totalTime() const
{
    if (m_frames.isEmpty())
        return 0.0;
    return m_frames.last().timestamp;
}

double Player::currentTime() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_frames.size())
        return 0.0;
    return m_frames.at(m_currentIndex).timestamp;
}

void Player::play()
{
    if (m_frames.isEmpty())
        return;

    if (m_currentIndex >= m_frames.size() - 1)
        m_currentIndex = 0;

    m_playing = true;
    m_tickStartMs = QDateTime::currentMSecsSinceEpoch();
    m_playbackBaseTime = m_frames.at(m_currentIndex).timestamp;
    m_timer.start();
    emit stateChanged(true);
}

void Player::pause()
{
    if (!m_playing)
        return;
    m_playing = false;
    m_timer.stop();
    emit stateChanged(false);
}

void Player::stop()
{
    m_playing = false;
    m_timer.stop();
    m_currentIndex = 0;
    emit stateChanged(false);
    emitProgress();
}

void Player::seekTo(double seconds)
{
    bool wasPlaying = m_playing;
    if (wasPlaying)
        m_timer.stop();

    // 二分查找目标时间戳
    auto it = std::lower_bound(m_frames.begin(), m_frames.end(), seconds,
        [](const CanFrame &f, double t) { return f.timestamp < t; });
    m_currentIndex = static_cast<int>(it - m_frames.begin());
    if (m_currentIndex >= m_frames.size())
        m_currentIndex = m_frames.size() - 1;

    if (wasPlaying) {
        m_tickStartMs = QDateTime::currentMSecsSinceEpoch();
        m_playbackBaseTime = m_frames.at(m_currentIndex).timestamp;
        m_timer.start();
    }
    emitProgress();
}

void Player::setSpeed(double speed)
{
    if (speed <= 0)
        return;

    if (m_playing) {
        // 调整基准以保持连续
        double elapsedReal = (QDateTime::currentMSecsSinceEpoch() - m_tickStartMs) / 1000.0;
        double elapsedFrame = elapsedReal * m_speed;
        m_playbackBaseTime += elapsedFrame;
        m_tickStartMs = QDateTime::currentMSecsSinceEpoch();

        // 更新当前索引
        double target = m_playbackBaseTime;
        auto it = std::lower_bound(m_frames.begin(), m_frames.end(), target,
            [](const CanFrame &f, double t) { return f.timestamp < t; });
        m_currentIndex = static_cast<int>(it - m_frames.begin());
    }

    m_speed = speed;
}

void Player::onTick()
{
    if (!m_playing || m_frames.isEmpty())
        return;

    double elapsedReal = (QDateTime::currentMSecsSinceEpoch() - m_tickStartMs) / 1000.0;
    double targetTime = m_playbackBaseTime + elapsedReal * m_speed;

    // 发射所有时间戳 <= targetTime 的帧
    while (m_currentIndex < m_frames.size() &&
           m_frames.at(m_currentIndex).timestamp <= targetTime) {
        emit framePlayed(m_frames.at(m_currentIndex));
        ++m_currentIndex;
    }

    if (m_currentIndex >= m_frames.size()) {
        m_playing = false;
        m_timer.stop();
        emit stateChanged(false);
        emit finished();
    }

    emitProgress();
}

void Player::emitProgress()
{
    emit progressChanged(m_currentIndex, m_frames.size(), currentTime(), totalTime());
}
