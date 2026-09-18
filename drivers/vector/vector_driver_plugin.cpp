#include "vector_driver_plugin.h"

#include "core/candevice_vector.h"

QStringList VectorDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceVector::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> VectorDriverPlugin::enumerateDevices() const
{
    return CanDeviceVector::enumerate();
}

ICanDevice *VectorDriverPlugin::createDevice(int subType) const
{
    // subType = XL channelIndex (DeviceInfo::deviceType)
    return new CanDeviceVector(subType);
}
