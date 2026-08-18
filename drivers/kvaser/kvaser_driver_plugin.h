#ifndef KVASER_DRIVER_PLUGIN_H
#define KVASER_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief Kvaser 驱动插件 — CanDeviceKvaser 的外置 .odp 包装（阶段 3 迁移）
 *
 * 复用 src/core/candevice_kvaser.cpp 全部实现（同一份源码编入本 target），
 * 厂商 DLL（canlib32.dll）由 CanDeviceKvaser 内部按 drivers/kvaser/vendor →
 * 应用目录 → PATH 顺序加载（方案 §7.3）。
 */
class KvaserDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("kvaser"); }
    QString displayName() const override { return QStringLiteral("Kvaser"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // KVASER_DRIVER_PLUGIN_H
