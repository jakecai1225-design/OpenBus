#include "slcan_driver_plugin.h"

#include "core/candevice_slcan.h"

QStringList SlcanDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceSlcan::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> SlcanDriverPlugin::enumerateDevices() const
{
    // 无厂商 SDK：系统串口即设备（零副作用枚举，V 固件名在 open 阶段获取）
    return CanDeviceSlcan::enumerate();
}

ICanDevice *SlcanDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceSlcan(subType);
}
