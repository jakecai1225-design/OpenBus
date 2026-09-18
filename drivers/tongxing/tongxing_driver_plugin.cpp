#include "tongxing_driver_plugin.h"

#include "core/candevice_tongxing.h"

QStringList TongXingDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceTongXing::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> TongXingDriverPlugin::enumerateDevices() const
{
    return CanDeviceTongXing::enumerate();
}

ICanDevice *TongXingDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceTongXing(subType);
}
