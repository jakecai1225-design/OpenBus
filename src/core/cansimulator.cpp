#include "cansimulator.h"
#include <QRandomGenerator>

// 静态成员定义（C++17 constexpr inline 可省略，但某些编译器仍需此定义）
constexpr quint32 CanSimulator::STD_IDS[];
constexpr quint32 CanSimulator::EXT_IDS[];

CanSimulator::CanSimulator(QObject *parent)
    : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(5); // 默认 5ms 一帧 (~200 fps)
    connect(&m_timer, &QTimer::timeout, this, &CanSimulator::generateFrame);
}

void CanSimulator::start()
{
    if (m_running)
        return;
    m_running = true;
    m_elapsed = 0.0;
    m_counter = 0;
    m_timer.start();
}

void CanSimulator::stop()
{
    m_running = false;
    m_timer.stop();
}

void CanSimulator::generateFrame()
{
    CanFrame frame;
    frame.channel = m_channel;
    frame.direction = CanFrame::Rx;

    m_counter++;
    m_elapsed += m_timer.interval() / 1000.0;
    frame.timestamp = m_elapsed;

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
        // FD DLC: 8, 12, 16, 20, 24, 32, 48, 64
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

    emit frameGenerated(frame);
}
