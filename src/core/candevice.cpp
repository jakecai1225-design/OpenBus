#include "candevice.h"
#include "candevice_zlg.h"
#include "candevice_peak.h"
#include "candevice_kvaser.h"
#include "driver/driverregistry.h"
#include "logging.h"

// ---- 品牌显示名 ----

QString ICanDevice::brandName(Brand b)
{
    switch (b) {
    case Brand::ZLG:      return QStringLiteral("ZLG");
    case Brand::PEAK:     return QStringLiteral("PEAK");
    case Brand::Kvaser:   return QStringLiteral("Kvaser");
    case Brand::TongXing: return QStringLiteral("同星");
    case Brand::SLCAN:    return QStringLiteral("SLCAN");
    }
    return QStringLiteral("Unknown");
}

// ---- 全品牌枚举（委托 DriverRegistry：内置 + 外置聚合，外置优先） ----

std::vector<ICanDevice::DeviceInfo> ICanDevice::enumerateAll()
{
    return DriverRegistry::instance()->enumerateDevices();
}

// ---- 工厂方法（委托 DriverRegistry：外置驱动优先于内置后端） ----

std::unique_ptr<ICanDevice> ICanDevice::create(Brand brand, int subType)
{
    const QString driverId = DriverRegistry::brandToDriverId(brand);
    std::unique_ptr<ICanDevice> dev(
        DriverRegistry::instance()->createDevice(driverId, subType));
    if (!dev) {
        OPENBUS_LOG_WARN("ICanDevice", "create failed: driver '{}' (subType {})",
                         driverId.toStdString(), subType);
    }
    return dev;
}
