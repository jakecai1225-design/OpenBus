#include "cansimulator.h"
#include "logging.h"

#include <QRandomGenerator>

// 静态成员定义
constexpr quint32 CanSimulator::STD_IDS[];
constexpr quint32 CanSimulator::EXT_IDS[];

CanSimulator::CanSimulator(QObject *parent)
    : QObject(parent)
{
    m_drainTimer.setTimerType(Qt::PreciseTimer);
    m_drainTimer.setInterval(2); // 每 2ms 批量消费队列
    connect(&m_drainTimer, &QTimer::timeout, this, &CanSimulator::drainQueue);
}

CanSimulator::~CanSimulator()
{
    stop();
}

void CanSimulator::setIntervalMs(int ms)
{
    m_intervalMs = ms;
}

void CanSimulator::start()
{
    if (m_running)
        return;
    m_running = true;

    // 启动后台生成线程
    m_workerThread = QThread::create([this]() { workerLoop(); });
    if (m_workerThread) {
        m_workerThread->start();
        SIN_LOG_DEBUG("CanSimulator", "worker thread started, interval={}ms", m_intervalMs);
    }

    // 启动主线程消费定时器
    m_drainTimer.start();
}

void CanSimulator::stop()
{
    m_running = false;
    m_drainTimer.stop();

    if (m_workerThread) {
        m_workerThread->wait(1000); // 等待最多 1 秒
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        SIN_LOG_DEBUG("CanSimulator", "worker thread stopped");
    }

    // 排空队列中残留帧
    drainQueue();
}

void CanSimulator::workerLoop()
{
    double elapsed = 0.0;

    while (m_running.load(std::memory_order_relaxed)) {
        CanFrame frame;
        frame.channel = m_channel;
        frame.direction = CanFrame::Rx;

        elapsed += m_intervalMs / 1000.0;
        frame.timestamp = elapsed;

        auto *rng = QRandomGenerator::global();

        // 70% 经典标准帧, 15% CAN FD 标准帧, 10% 扩展帧, 5% CAN FD 扩展帧
        int r = rng->bounded(100);

        if (r < 70) {
            // 经典 CAN 标准帧
            frame.id = STD_IDS[rng->bounded(8)];
            frame.extended = false;
            frame.fd = false;
            int dlc = rng->bounded(9); // 0-8
            frame.dlc = static_cast<quint8>(dlc);
            frame.data.resize(dlc);
            for (int i = 0; i < dlc; ++i)
                frame.data[i] = static_cast<char>(rng->bounded(256));
        } else if (r < 85) {
            // CAN FD 标准帧
            frame.id = STD_IDS[rng->bounded(8)];
            frame.extended = false;
            frame.fd = true;
            frame.bitrateSwitch = rng->bounded(2);
            static const quint8 fdDlcs[] = {8, 12, 16, 20, 24, 32, 48, 64};
            int len = fdDlcs[rng->bounded(8)];
            frame.dlc = CanFrame::lengthToDlc(len);
            frame.data.resize(len);
            for (int i = 0; i < len; ++i)
                frame.data[i] = static_cast<char>(rng->bounded(256));
        } else if (r < 95) {
            // 经典扩展帧
            frame.id = EXT_IDS[rng->bounded(3)];
            frame.extended = true;
            frame.fd = false;
            int dlc = rng->bounded(9);
            frame.dlc = static_cast<quint8>(dlc);
            frame.data.resize(dlc);
            for (int i = 0; i < dlc; ++i)
                frame.data[i] = static_cast<char>(rng->bounded(256));
        } else {
            // CAN FD 扩展帧
            frame.id = EXT_IDS[rng->bounded(3)];
            frame.extended = true;
            frame.fd = true;
            frame.bitrateSwitch = rng->bounded(2);
            static const quint8 fdDlcs[] = {8, 12, 16, 20, 24, 32, 48, 64};
            int len = fdDlcs[rng->bounded(8)];
            frame.dlc = CanFrame::lengthToDlc(len);
            frame.data.resize(len);
            for (int i = 0; i < len; ++i)
                frame.data[i] = static_cast<char>(rng->bounded(256));
        }

        // 入队（无锁，不阻塞）
        m_queue.enqueue(std::move(frame));

        // 按间隔休眠
        QThread::msleep(m_intervalMs);
    }
}

void CanSimulator::drainQueue()
{
    QVector<CanFrame> frames;
    // 批量出队，最多 512 帧/次（避免单次消费时间过长阻塞 UI）
    size_t got = m_queue.tryDequeueBulk(frames, 512);
    if (got == 0)
        return;

    for (const auto &frame : frames)
        emit frameGenerated(frame);
}
