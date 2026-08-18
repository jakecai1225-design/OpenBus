#include "zlg_driver_plugin.h"

#include "core/candevice_zlg.h"

QStringList ZlgDriverPlugin::deviceTypes() const
{
    QStringList types;
    const auto devs = CanDeviceZLG::enumerate();
    for (const auto &d : devs)
        types << QString::number(d.deviceType);
    return types;
}

std::vector<ICanDevice::DeviceInfo> ZlgDriverPlugin::enumerateDevices() const
{
    // 复用内置后端的枚举（内部 QLibrary 按 §7.3 顺序定位 zlgcan.dll）
    return CanDeviceZLG::enumerate();
}

ICanDevice *ZlgDriverPlugin::createDevice(int subType) const
{
    return new CanDeviceZLG(static_cast<CanDeviceZLG::DeviceType>(subType));
}
