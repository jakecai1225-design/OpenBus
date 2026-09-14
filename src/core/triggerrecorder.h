#ifndef TRIGGERRECORDER_H
#define TRIGGERRECORDER_H

#include <QObject>
#include <QQueue>
#include <memory>
#include "core/canframe.h"
#include "core/filter_engine.h"
#include "core/logsplitter.h"

/**
 * @brief 条件触发录制器（对标 CANoe Trigger Logging）
 *
 * 工作流程：
 *   1. 始终在内存环形缓冲区中缓存最近 N 秒/N 帧的报文（Pre-Trigger）
 *   2. 每帧通过 FilterEngine 求值触发条件
 *   3. 触发后：将前置缓冲写入文件 → 持续录制 → 达到后置时长后停止
 *   4. 支持单次触发和重复触发模式
 */
class TriggerRecorder : public QObject
{
    Q_OBJECT

public:
    struct Config {
        LogSplitter::Config logConfig;  ///< 日志分片配置
        QString triggerExpr;             ///< 触发条件表达式（FilterEngine 语法）
        double preTriggerSeconds = 5.0;  ///< 前置缓冲时长 (秒)
        double postTriggerSeconds = 10.0; ///< 后置录制时长 (秒)
        bool repeatTrigger = true;       ///< 重复触发模式
    };

    enum class State {
        Idle,        ///< 等待触发
        PreTrigger,  ///< 已触发，写入前置缓冲
        Recording    ///< 持续录制
    };

    explicit TriggerRecorder(QObject *parent = nullptr);

    bool start(const Config &config);
    void stop();

    bool isRunning() const { return m_running; }
    State state() const { return m_state; }

public slots:
    void onFrame(const CanFrame &frame);
    void onFrames(const QVector<CanFrame> &frames);

signals:
    void triggered(const QString &expr, double timestamp);
    void recordingStarted(const QString &filePath);
    void recordingStopped(const QString &filePath, int totalFrames);
    void frameRecorded(int totalFrames);

private:
    Config m_config;
    FilterEngine m_triggerFilter;
    LogSplitter *m_splitter = nullptr;
    State m_state = State::Idle;
    bool m_running = false;

    /// 前置缓冲环形队列
    QQueue<CanFrame> m_preBuffer;
    int m_preBufferMaxFrames = 5000;

    double m_triggerTime = 0.0;
    double m_recordEndTime = 0.0;
    int m_frameCount = 0;

    void checkTrigger(const CanFrame &frame);
    void flushPreBuffer();
};

#endif // TRIGGERRECORDER_H
