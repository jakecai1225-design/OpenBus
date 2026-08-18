#ifndef SLCAN_DRIVER_PLUGIN_H
#define SLCAN_DRIVER_PLUGIN_H

#include "core/driver/candriverplugin.h"

#include <QObject>

/**
 * @brief SLCAN 驱动插件 — CanDeviceSlcan 的外置 .odp 包装（方案 §14 v2.2）
 *
 * 一份驱动覆盖 Lawicel 串口文本协议全家（§14.4 P0-A）：淘宝廉价适配器 /
 * Lawicel CANUSB / CANable (slcan 固件) / USBtin / ESP32·Arduino DIY。
 * 复用 src/core/candevice_slcan.cpp 全部实现（同一份源码编入本 target）；
 * 串口走系统驱动（Win32 API），无厂商 SDK、无随包 DLL。
 * 串口选择经 vendorCtrl(VendorCmdSetPort) 指定（连接页下拉）。
 */
class SlcanDriverPlugin : public QObject, public CanDriverPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.sin.openbus.CanDriverPlugin/1.0" FILE "driver.json")
    Q_INTERFACES(CanDriverPlugin)

public:
    QString driverId() const override { return QStringLiteral("slcan"); }
    QString displayName() const override { return QStringLiteral("SLCAN"); }
    QString version() const override { return QStringLiteral("1.0.0"); }
    QStringList deviceTypes() const override;
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const override;
    ICanDevice *createDevice(int subType) const override;
};

#endif // SLCAN_DRIVER_PLUGIN_H
