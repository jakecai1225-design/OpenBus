#ifndef VECTOR_DRIVER_PLUGIN_H
#define VECTOR_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief Vector XL plugin — thin wrap of CanDeviceVector for .odp / external load.
 *
 * Reuses src/core/candevice_vector.cpp. Vendor DLL (vxlapi64.dll) is loaded by
 * CanDeviceVector: drivers/vector/vendor → app dir → PATH.
 */
class VectorDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("vector"); }
    QString displayName() const override { return QStringLiteral("Vector XL"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // VECTOR_DRIVER_PLUGIN_H
