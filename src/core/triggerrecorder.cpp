#include "triggerrecorder.h"

TriggerRecorder::TriggerRecorder(QObject *parent)
    : QObject(parent)
{
    m_splitter = new LogSplitter(this);
    connect(m_splitter, &LogSplitter::frameRecorded,
            this, &TriggerRecorder::frameRecorded);
}

bool TriggerRecorder::start(const Config &config)
{
    m_config = config;
    m_running = true;
    m_state = State::Idle;
    m_preBuffer.clear();
    m_frameCount = 0;
    m_triggerTime = 0.0;
    m_recordEndTime = 0.0;

    // 估算前置缓冲帧数上限
    // 假设最高 1000 帧/s，preTriggerSeconds 秒 → 上限 = preTriggerSeconds * 1000
    m_preBufferMaxFrames = static_cast<int>(m_config.preTriggerSeconds * 1000);
    if (m_preBufferMaxFrames < 100) m_preBufferMaxFrames = 100;
    if (m_preBufferMaxFrames > 100000) m_preBufferMaxFrames = 100000;

    // 编译触发条件表达式
    if (!m_config.triggerExpr.isEmpty()) {
        if (!m_triggerFilter.compile(m_config.triggerExpr)) {
            // 表达式编译失败，不启动
            m_running = false;
            return false;
        }
    }

    return true;
}

void TriggerRecorder::stop()
{
    if (!m_running) return;

    if (m_state == State::Recording) {
        m_splitter->stop();
        emit recordingStopped(m_splitter->property("currentPath").toString(), m_frameCount);
    }

    m_running = false;
    m_state = State::Idle;
    m_preBuffer.clear();
}

void TriggerRecorder::onFrame(const CanFrame &frame)
{
    if (!m_running) return;

    switch (m_state) {
    case State::Idle: {
        // 缓存前置帧
        m_preBuffer.enqueue(frame);
        while (m_preBuffer.size() > m_preBufferMaxFrames)
            m_preBuffer.dequeue();

        // 同时维护时间窗口：丢弃超过 preTriggerSeconds 的旧帧
        double cutoff = frame.timestamp - m_config.preTriggerSeconds;
        while (!m_preBuffer.isEmpty() && m_preBuffer.head().timestamp < cutoff)
            m_preBuffer.dequeue();

        // 检查触发条件
        checkTrigger(frame);
        break;
    }

    case State::PreTrigger: {
        // 前置缓冲刷新中 → 直接写入文件
        m_splitter->recordFrame(frame);
        m_frameCount++;
        if (frame.timestamp >= m_triggerTime) {
            m_state = State::Recording;
            m_recordEndTime = frame.timestamp + m_config.postTriggerSeconds;
        }
        break;
    }

    case State::Recording: {
        m_splitter->recordFrame(frame);
        m_frameCount++;

        // 检查后置时长结束
        if (frame.timestamp >= m_recordEndTime) {
            m_splitter->stop();
            emit recordingStopped(QString(), m_frameCount);
            m_frameCount = 0;

            if (m_config.repeatTrigger) {
                m_state = State::Idle;
                m_preBuffer.clear();
            } else {
                m_running = false;
                m_state = State::Idle;
            }
        }
        break;
    }
    }
}

void TriggerRecorder::checkTrigger(const CanFrame &frame)
{
    if (m_state != State::Idle) return;

    bool isTriggered = false;
    if (m_triggerFilter.isEmpty()) {
        // 无条件 → 立即触发
        isTriggered = true;
    } else {
        isTriggered = m_triggerFilter.evaluate(frame);
    }

    if (isTriggered) {
        m_triggerTime = frame.timestamp;
        m_state = State::PreTrigger;
        emit triggered(m_config.triggerExpr, frame.timestamp);

        // 启动分片录制器
        m_splitter->start(m_config.logConfig);
        emit recordingStarted(m_config.logConfig.directory + "/" + m_config.logConfig.prefix);

        // 将前置缓冲写入文件
        flushPreBuffer();
    }
}

void TriggerRecorder::flushPreBuffer()
{
    for (const auto &frame : m_preBuffer) {
        m_splitter->recordFrame(frame);
        m_frameCount++;
    }
    m_preBuffer.clear();
}
