#ifndef PEAK_DRIVER_PLUGIN_H
#define PEAK_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief PEAK PCAN plugin — thin wrap of CanDevicePEAK (PCAN-Basic)
 *
 * Loads PCANBasic.dll from drivers/peak/vendor → app dir → PATH.
 * Same API stack as PCAN-View / Cangaroo (not BusMaster CanApi2).
 */
class PeakDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("peak"); }
    QString displayName() const override { return QStringLiteral("PEAK PCAN"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // PEAK_DRIVER_PLUGIN_H
