#ifndef BUSMUST_DRIVER_PLUGIN_H
#define BUSMUST_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief BUSMUST driver plugin — CanDeviceBusmust wrapper (.odp layout)
 *
 * Device Manager: "BUSMUST USB-CAN(FD) Family" (VID_0810&PID_E122, etc.)
 * Stack: Windows WinUSB/KMDF → BMAPI64.dll → CanDeviceBusmust
 */
class BusmustDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("busmust"); }
    QString displayName() const override { return QStringLiteral("BUSMUST USB-CAN(FD)"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // BUSMUST_DRIVER_PLUGIN_H
