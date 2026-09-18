#ifndef INTREPID_DRIVER_PLUGIN_H
#define INTREPID_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief Intrepid plugin — thin wrap of CanDeviceIntrepid.
 *
 * Vendor shared library (icsneoc) is loaded by CanDeviceIntrepid:
 * drivers/intrepid/vendor -> app dir -> PATH / ld cache.
 */
class IntrepidDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("intrepid"); }
    QString displayName() const override { return QStringLiteral("Intrepid"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // INTREPID_DRIVER_PLUGIN_H
