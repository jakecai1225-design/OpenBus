#include "ixxat_driver_plugin.h"

#include "core/candevice_ixxat.h"

QStringList IxxatDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceIxxat::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> IxxatDriverPlugin::enumerateDevices() const
{
    return CanDeviceIxxat::enumerate();
}

ICanDevice *IxxatDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceIxxat(subType);
}
