#include "socketcan_driver_plugin.h"

#include "core/candevice_socketcan.h"

QStringList SocketCanDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceSocketCan::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> SocketCanDriverPlugin::enumerateDevices() const
{
    return CanDeviceSocketCan::enumerate();
}

ICanDevice *SocketCanDriverPlugin::createDevice(int subType) const
{
    // subType = kernel ifindex (DeviceInfo::deviceType)
    return new CanDeviceSocketCan(subType);
}
