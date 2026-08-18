#ifndef PEAK_DRIVER_PLUGIN_H
#define PEAK_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief PEAK 驱动插件 — CanDevicePEAK 的外置 .odp 包装（阶段 3 迁移）
 *
 * 复用 src/core/candevice_peak.cpp 全部实现（同一份源码编入本 target），
 * 厂商 DLL（PCANUSB.dll）由 CanDevicePEAK 内部按 drivers/peak/vendor →
 * 应用目录 → PATH 顺序加载（方案 §7.3）。
 */
class PeakDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("peak"); }
    QString displayName() const override { return QStringLiteral("PEAK System"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // PEAK_DRIVER_PLUGIN_H
