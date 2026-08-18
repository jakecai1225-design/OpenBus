#ifndef CANDRIVERPLUGIN_H
#define CANDRIVERPLUGIN_H

#include "core/candevice.h"
#include <QtPlugin>
#include <QString>
#include <QStringList>
#include <vector>

/**
 * @brief CAN 驱动插件接口 — 主进程内原生驱动（QPluginLoader 加载）
 *
 * 驱动包(.odp)安装到 drivers/<id>/ 后由 DriverRegistry 加载。
 * 实现方：QObject + 本接口 + Q_PLUGIN_METADATA（见 drivers/ SDK 模板）。
 *
 * ABI 契约（见 doc/驱动系统方案.md §3.2）：
 *   - 与主程序同 Qt 大版本（6.8.x）+ 同编译器（MinGW-w64 13.1, C++17, x86_64）
 *   - 跨 DLL 边界仅传 Qt 值类型 / POD / ICanDevice* 裸指针
 *   - 接口只增不改：新增虚函数追加在末尾并升 IID 版本（/2.0）
 */
class CanDriverPlugin
{
public:
    virtual ~CanDriverPlugin() = default;

    /// 驱动唯一标识（小写，如 "zlg" / "peak" / "candle" / "slcan"）
    virtual QString driverId() const = 0;

    /// 显示名（如 "ZLG 致远电子"）
    virtual QString displayName() const = 0;

    /// 驱动版本（与 driver.json 一致）
    virtual QString version() const = 0;

    /// 支持的设备子类型（与 driver.json devices[].type 对应）
    virtual QStringList deviceTypes() const = 0;

    /// 枚举当前可用物理设备（内部 QLibrary 加载厂商 DLL，失败返回空）
    virtual std::vector<ICanDevice::DeviceInfo> enumerateDevices() const = 0;

    /// 创建设备实例（返回裸指针，调用方 unique_ptr 接管生命周期）
    /// @param subType driver.json devices[].type
    virtual ICanDevice *createDevice(int subType) const = 0;
};

Q_DECLARE_INTERFACE(CanDriverPlugin, "com.sin.openbus.CanDriverPlugin/1.0")

#endif // CANDRIVERPLUGIN_H
