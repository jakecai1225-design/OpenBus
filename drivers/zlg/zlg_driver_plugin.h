#ifndef ZLG_DRIVER_PLUGIN_H
#define ZLG_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief ZLG 驱动插件 — CanDeviceZLG 的外置 .odp 包装（首个驱动包样例）
 *
 * 复用 src/core/candevice_zlg.cpp 全部实现（同一份源码编入本 target），
 * 厂商 DLL 由 CanDeviceZLG 内部按 drivers/zlg/vendor → 应用目录 →
 * ZCANPRO → PATH 顺序加载（方案 §7.3）。
 */
class ZlgDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("zlg"); }
    QString displayName() const override { return QStringLiteral("ZLG 致远电子"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // ZLG_DRIVER_PLUGIN_H
