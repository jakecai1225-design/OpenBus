#include "busmust_driver_plugin.h"

#include "core/candevice_busmust.h"

QStringList BusmustDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceBusmust::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> BusmustDriverPlugin::enumerateDevices() const
{
    return CanDeviceBusmust::enumerate();
}

ICanDevice *BusmustDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceBusmust(subType);
}
