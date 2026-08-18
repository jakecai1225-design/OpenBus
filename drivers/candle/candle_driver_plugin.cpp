#include "candle_driver_plugin.h"

#include "core/candevice_candle.h"

QStringList CandleDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceCandle::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> CandleDriverPlugin::enumerateDevices() const
{
    // VID/PID 白名单 + DEVICE_CONFIG 探测（内部经 QLibrary 定位 libusb-1.0.dll）
    return CanDeviceCandle::enumerate();
}

ICanDevice *CandleDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceCandle(subType);
}
