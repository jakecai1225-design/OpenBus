#include "intrepid_driver_plugin.h"

#include "core/candevice_intrepid.h"

QStringList IntrepidDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceIntrepid::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> IntrepidDriverPlugin::enumerateDevices() const
{
    return CanDeviceIntrepid::enumerate();
}

ICanDevice *IntrepidDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceIntrepid(subType);
}
