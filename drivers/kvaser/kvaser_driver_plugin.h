#ifndef KVASER_DRIVER_PLUGIN_H
#define KVASER_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief Kvaser CANlib plugin — thin wrap of CanDeviceKvaser for .odp / external load.
 *
 * Reuses src/core/candevice_kvaser.cpp. Vendor DLL (canlib32.dll) is loaded by
 * CanDeviceKvaser: drivers/kvaser/vendor → app dir → PATH.
 */
class KvaserDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("kvaser"); }
    QString displayName() const override { return QStringLiteral("Kvaser"); }
    QString version() const override { return QStringLiteral("1.1.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // KVASER_DRIVER_PLUGIN_H
