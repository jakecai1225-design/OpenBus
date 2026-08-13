#include "candevice.h"
#include "candevice_zlg.h"
#include "candevice_peak.h"
#include "candevice_kvaser.h"
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

// ---- 全品牌枚举 ----

std::vector<ICanDevice::DeviceInfo> ICanDevice::enumerateAll()
{
    std::vector<DeviceInfo> list;

    // ZLG
    if (CanDeviceZLG::isAvailable()) {
        auto devs = CanDeviceZLG::enumerate();
        for (auto &d : devs) {
            d.brand = Brand::ZLG;
            list.push_back(d);
        }
    }

    // PEAK
    if (CanDevicePEAK::isAvailable()) {
        auto devs = CanDevicePEAK::enumerate();
        for (auto &d : devs) {
            d.brand = Brand::PEAK;
            list.push_back(d);
        }
    }

    // Kvaser
    if (CanDeviceKvaser::isAvailable()) {
        auto devs = CanDeviceKvaser::enumerate();
        for (auto &d : devs) {
            d.brand = Brand::Kvaser;
            list.push_back(d);
        }
    }

    // 同星、SLCAN 待扩展
    return list;
}

// ---- 工厂方法 ----

std::unique_ptr<ICanDevice> ICanDevice::create(Brand brand, int subType)
{
    switch (brand) {
    case Brand::ZLG:
        return std::make_unique<CanDeviceZLG>(
            static_cast<CanDeviceZLG::DeviceType>(subType));

    case Brand::PEAK:
        return std::make_unique<CanDevicePEAK>(
            static_cast<CanDevicePEAK::DeviceType>(subType));

    case Brand::Kvaser:
        return std::make_unique<CanDeviceKvaser>(subType);

    default:
        OPENBUS_LOG_WARN("ICanDevice", "brand {} not implemented yet",
                     static_cast<int>(brand));
        return nullptr;
    }
}
