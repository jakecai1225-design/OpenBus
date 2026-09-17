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
    // PCAN_ATTACHED_CHANNELS via PCANBasic.dll
    return CanDevicePEAK::enumerate();
}

ICanDevice *PeakDriverPlugin::createDevice(int subType) const
{
    // subType = TPCANHandle from enumerate (e.g. 0x51 = PCAN_USBBUS1)
    return new CanDevicePEAK(subType);
}
