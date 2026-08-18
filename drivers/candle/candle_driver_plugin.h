#ifndef CANDLE_DRIVER_PLUGIN_H
#define CANDLE_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief Candle/GS_USB 驱动插件 — CanDeviceCandle 的外置 .odp 包装（方案 §14 v2.2）
 *
 * 一份驱动覆盖 GS_USB 协议全家（§14.4 P0-B）：CANable (candle 固件) /
 * candleLight DIY / CANnectivity / CES CANext FD / ABE CANDebugger /
 * Xylanta Saint3。复用 src/core/candevice_candle.cpp 全部实现
 * （同一份源码编入本 target）。
 * libusb-1.0.dll 由 CanDeviceCandle 内部按 drivers/candle/vendor →
 * 应用目录 → PATH 顺序动态加载（方案 §7.3），永不卸载。
 * 设备能力（CAN FD / 硬件时间戳）在 open 阶段经 BT_CONST 特征位探测。
 */
class CandleDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("candle"); }
    QString displayName() const override { return QStringLiteral("Candle / GS_USB"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // CANDLE_DRIVER_PLUGIN_H
