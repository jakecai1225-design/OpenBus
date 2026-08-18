#include "peak_driver_plugin.h"

#include "core/candevice_peak.h"

QStringList PeakDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDevicePEAK::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> PeakDriverPlugin::enumerateDevices() const
{
    // 复用内置后端的枚举（内部 QLibrary 按 §7.3 顺序定位 PCANUSB.dll）
    return CanDevicePEAK::enumerate();
}

ICanDevice *PeakDriverPlugin::createDevice(int subType) const
{
    return new CanDevicePEAK(static_cast<CanDevicePEAK::DeviceType>(subType));
}
