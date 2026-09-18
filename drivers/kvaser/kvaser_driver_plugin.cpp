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
    return CanDeviceKvaser::enumerate();
}

ICanDevice *KvaserDriverPlugin::createDevice(int subType) const
{
    // subType = CANlib channel number (DeviceInfo::deviceType)
    return new CanDeviceKvaser(subType);
}
