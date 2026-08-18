#include "kvaser_driver_plugin.h"

#include "core/candevice_kvaser.h"

QStringList KvaserDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceKvaser::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> KvaserDriverPlugin::enumerateDevices() const
{
    // 复用内置后端的枚举（内部 QLibrary 按 §7.3 顺序定位 canlib32.dll）
    return CanDeviceKvaser::enumerate();
}

ICanDevice *KvaserDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceKvaser(subType);
}
