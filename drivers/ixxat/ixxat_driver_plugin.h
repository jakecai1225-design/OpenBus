#ifndef IXXAT_DRIVER_PLUGIN_H
#define IXXAT_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief IXXAT VCI4 plugin — thin wrap of CanDeviceIxxat.
 *
 * Vendor DLL (vcinpl2.dll) is loaded by CanDeviceIxxat:
 * drivers/ixxat/vendor -> app dir -> PATH.
 */
class IxxatDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("ixxat"); }
    QString displayName() const override { return QStringLiteral("IXXAT"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // IXXAT_DRIVER_PLUGIN_H
