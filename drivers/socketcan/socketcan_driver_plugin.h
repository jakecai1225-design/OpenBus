#ifndef SOCKETCAN_DRIVER_PLUGIN_H
#define SOCKETCAN_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief SocketCAN plugin — thin wrap of CanDeviceSocketCan.
 *
 * Linux-only; on Windows isAvailable() is false and enumerate is empty.
 */
class SocketCanDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("socketcan"); }
    QString displayName() const override { return QStringLiteral("SocketCAN"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // SOCKETCAN_DRIVER_PLUGIN_H
