#ifndef PLAYER_H
#define PLAYER_H

#include <QObject>
#include <QTimer>
#include <QVector>
#include "core/canframe.h"

/**
 * @brief 报文回放器
 *
 * 从 .sin 录制文件加载帧序列，按原始时间戳回放。
 * 支持 play / pause / stop / seek / setSpeed。
 */
class Player : public QObject
{
    Q_OBJECT

public:
    explicit Player(QObject *parent = nullptr);

    /// 从文件加载帧序列（.sin 格式）
    bool load(const QString &filePath);

    /// 从帧序列直接加载（用于 ASC/BLF/CSV 等外部格式导入）
    void loadFrames(const QVector<CanFrame> &frames);

    /// 清空已加载的帧
    void unload();

    bool isPlaying() const { return m_playing; }
    bool isLoaded() const { return !m_frames.isEmpty(); }

    int totalFrames() const { return m_frames.size(); }
    int currentFrameIndex() const { return m_currentIndex; }

    double totalTime() const;
    double currentTime() const;

    double speed() const { return m_speed; }

public slots:
    void play();
    void pause();
    void stop();
    void seekTo(double seconds);
    void setSpeed(double speed);

signals:
    void framePlayed(const CanFrame &frame);
    void progressChanged(int currentFrame, int totalFrames,
                         double currentTime, double totalTime);
    void finished();
    void stateChanged(bool playing);

private slots:
    void onTick();

private:
    QVector<CanFrame> m_frames;
    bool m_playing = false;
    int m_currentIndex = 0;
    double m_speed = 1.0;

    qint64 m_tickStartMs = 0;     // QTimer 启动时的墙上时间
    double m_playbackBaseTime = 0.0; // 对应的帧时间偏移

    QTimer m_timer;

    void emitProgress();
};

#endif // PLAYER_H
