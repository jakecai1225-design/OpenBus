#include "player.h"
#include "core/canfileio/canfileio_factory.h"
#include "utils/canutils.h"

#include <QDateTime>
#include <algorithm>

Player::Player(QObject *parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    // 16 ms: stream frames to GUI; 1 ms starved ticks dump huge batches (stutter).
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &Player::onTick);
}

bool Player::load(const QString &filePath)
{
    stop();

    // 根据扩展名创建读取器
    auto reader = CanFileIOFactory::createReader(filePath);
    if (!reader || !reader->open(filePath))
        return false;

    m_frames.clear();
    int count = reader->readAll(m_frames);
    reader->close();

    if (count < 0)
        return false;

    for (auto &f : m_frames)
        CanUtils::syncTimestampNs(f);

    m_currentIndex = 0;
    emitProgress(true);
    return true;
}

void Player::loadFrames(const QVector<CanFrame> &frames)
{
    stop();
    m_frames = frames;
    // Keep file/measurement timestamps; only fill timestampNs if missing.
    // Never stamp with QDateTime::current* — Trace and Graphic share CanFrame::timestamp.
    for (auto &f : m_frames)
        CanUtils::syncTimestampNs(f);
    m_currentIndex = 0;
    emitProgress(true);
}

void Player::unload()
{
    stop();
    m_frames.clear();
    m_currentIndex = 0;
    emitProgress(true);
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
    emitProgress(true);
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
    emitProgress(true);
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

    // Stream: cap per tick so a starved timer / dense log cannot dump tens of thousands
    // of frames onto the GUI in one shot (Trace looked bursty, not continuous).
    constexpr int kMaxBatch = 256;
    QVector<CanFrame> batch;
    batch.reserve(qMin(kMaxBatch, m_frames.size() - m_currentIndex));
    while (m_currentIndex < m_frames.size() &&
           m_frames.at(m_currentIndex).timestamp <= targetTime &&
           batch.size() < kMaxBatch) {
        batch.append(m_frames.at(m_currentIndex));
        ++m_currentIndex;
    }
    if (!batch.isEmpty())
        emit framesPlayed(batch);

    if (batch.size() >= kMaxBatch && m_currentIndex < m_frames.size() &&
        m_frames.at(m_currentIndex).timestamp <= targetTime) {
        // Hold playback clock at last emitted timestamp so remaining frames stream.
        m_playbackBaseTime = batch.last().timestamp;
        m_tickStartMs = QDateTime::currentMSecsSinceEpoch();
    }

    if (m_currentIndex >= m_frames.size()) {
        if (m_loop) {
            m_currentIndex = 0;
            m_tickStartMs = QDateTime::currentMSecsSinceEpoch();
            m_playbackBaseTime = m_frames.at(0).timestamp;
        } else {
            m_playing = false;
            m_timer.stop();
            emit stateChanged(false);
            emit finished();
        }
    }

    emitProgress(false);
}

void Player::emitProgress(bool force)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_lastProgressMs != 0 && now - m_lastProgressMs < 50)
        return;
    m_lastProgressMs = now;
    emit progressChanged(m_currentIndex, m_frames.size(), currentTime(), totalTime());
}
