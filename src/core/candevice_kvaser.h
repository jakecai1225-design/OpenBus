#ifndef CANDEVICE_KVASER_H
#define CANDEVICE_KVASER_H

#include "core/candevice.h"
#include <QLibrary>
#include <QString>
#include <atomic>
#include <memory>

/**
 * @brief Kvaser CAN/CAN FD 设备后端 (P1)
 *
 * 动态加载 canlib32.dll / kvlclib.dll，封装 Kvaser CANLIB API。
 * 支持 Kvaser USBcan、Leaf 等系列设备。
 *
 * 架构：ICanDevice → CanDeviceKvaser → canlib32.dll (运行时加载)
 * DLL 缺失时 isAvailable() 返回 false，不影响其他后端。
 */
class CanDeviceKvaser : public ICanDevice
{
public:
    /// Kvaser 设备子类型
    enum DeviceType {
        USBcan2     = 0,   ///< Kvaser USBcan II
        Leaf        = 1,   ///< Kvaser Leaf
        LeafLight   = 2,   ///< Kvaser Leaf Light
        UsbCanHybrid = 3,  ///< Kvaser Hybrid
    };

    explicit CanDeviceKvaser(int subType = 0);
    ~CanDeviceKvaser() override;

    // ---- ICanDevice ----
    Brand brand() const override { return Brand::Kvaser; }
    bool open(int devIndex, int channel, int arbBaud, int dataBaud, bool canFd) override;
    void close() override;
    int send(const CanFrame &frame) override;
    int recv(int timeoutMs, std::vector<CanFrame> &outFrames) override;
    int pendingCount() const override;
    bool isOpen() const override;
    QString deviceName() const override;

    /// 检查 canlib32.dll 是否可加载
    static bool isAvailable();

    /// 枚举 Kvaser 设备
    static std::vector<DeviceInfo> enumerate();

private:
    bool loadDll();
    void unloadDll();

    QLibrary m_dll;
    int m_subType = 0;
    int m_devIndex = 0;
    int m_channel = 0;
    bool m_opened = false;
    bool m_canFd = false;
    int m_canlibHandle = -1;     ///< canlib 通道句柄
    QString m_deviceName;

    std::chrono::steady_clock::time_point m_startClock;
};

#endif // CANDEVICE_KVASER_H
