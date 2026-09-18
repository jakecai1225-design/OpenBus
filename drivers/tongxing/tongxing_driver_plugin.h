#ifndef TONGXING_DRIVER_PLUGIN_H
#define TONGXING_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief TOSUN libTSCAN plugin — thin wrap of CanDeviceTongXing.
 *
 * Vendor DLLs (libTSCAN.dll + libTSH.dll) are loaded by CanDeviceTongXing:
 * drivers/tongxing/vendor -> app dir -> PATH.
 */
class TongXingDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("tongxing"); }
    QString displayName() const override { return QStringLiteral("TOSUN"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // TONGXING_DRIVER_PLUGIN_H
