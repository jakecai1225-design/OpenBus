#ifndef CANSIMULATOR_H
#define CANSIMULATOR_H

#include <QObject>
#include <QTimer>
#include "core/canframe.h"

/**
 * @brief CAN 报文模拟器
 *
 * 无硬件环境下生成模拟 CAN/CAN FD 流量，用于界面演示和测试。
 * 生成多种标准 ID、扩展 ID、经典帧和 FD 帧的混合流量。
 */
class CanSimulator : public QObject
{
    Q_OBJECT

public:
    explicit CanSimulator(QObject *parent = nullptr);

    bool isRunning() const { return m_running; }
    void setChannel(quint8 ch) { m_channel = ch; }
    void setIntervalMs(int ms) { m_timer.setInterval(ms); }

public slots:
    void start();
    void stop();

signals:
    void frameGenerated(const CanFrame &frame);

private slots:
    void generateFrame();

private:
    bool m_running = false;
    QTimer m_timer;
    quint8 m_channel = 1;
    double m_elapsed = 0.0;
    int m_counter = 0;

    // 预定义 ID
    static constexpr quint32 STD_IDS[] = {0x100, 0x200, 0x300, 0x400, 0x500,
                                           0x600, 0x700, 0x7FF};
    static constexpr quint32 EXT_IDS[] = {0x18FEF100, 0x18FFA827, 0x18EAFFFE};
};

#endif // CANSIMULATOR_H
