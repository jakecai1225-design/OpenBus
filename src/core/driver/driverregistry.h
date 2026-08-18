#ifndef DRIVERREGISTRY_H
#define DRIVERREGISTRY_H

#include "core/candevice.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

class QPluginLoader;
class CanDriverPlugin;

/**
 * @brief 驱动注册表 — 内置驱动与外置 .odp 驱动的唯一装配点
 *
 * 职责（doc/驱动系统方案.md §四/§七）：
 *   - 启动注册内置驱动（ZLG/PEAK/Kvaser 静态编译后端，零安装开箱可用）
 *   - 扫描 <应用目录>/drivers/<id> 外置驱动：driver.json 清单 → sha256 校验 →
 *     QPluginLoader::metaData() ABI/架构预检 → load() → 注册条目
 *   - 对外提供 drivers() 元数据 / enumerateDevices() 聚合枚举 / createDevice() 工厂
 *   - 同 driverId 外置版本高者优先，可覆盖内置
 *
 * 生命周期：instance() 构造时注册内置；MainWindow 启动尾部调用 initialize()
 * 扫描外置；安装新驱动后调用 scanAndLoad() 热加载（无需重启）。
 */
class DriverRegistry : public QObject
{
    Q_OBJECT
public:
    /// 驱动条目（设备树 / 新增设备标签页 / 连接页的数据来源）
    struct DriverEntry {
        QString driverId;           ///< 唯一标识（小写）
        QString displayName;        ///< 显示名（如 "ZLG 致远电子"）
        QString version;            ///< 驱动版本
        QString iconPath;           ///< 外置驱动图标（drivers/<id>/icon.svg），内置为空
        bool builtin = false;       ///< 内置（静态编译）驱动
        bool loaded = false;        ///< 外置 .dll 已成功加载
        bool available = false;     ///< 当前可用（内置=厂商 DLL 可加载，外置=loaded）
        QString disabledReason;     ///< 不可用/未加载原因（预检失败、版本低等）
        bool enabled = true;         ///< 是否启用（禁用：设备树隐藏，不参与枚举/创建）
        QJsonArray devices;         ///< 设备型号表 [{type,name,channels,canFd}]
        int deviceKind = -1;        ///< CanDeviceManager::DeviceKind 兼容映射（未知=-1）
        ICanDevice::Brand brand = ICanDevice::Brand::SLCAN;  ///< Brand 兼容映射
        QString installDir;         ///< 外置驱动安装目录（内置为空）
    };

    static DriverRegistry *instance();

    /// 启动扫描 + 加载外置驱动（幂等；首次调用后 emit driversChanged）
    void initialize();

    /// 热安装后重扫（增量：新驱动加载，已加载的同 id 且未变化则跳过）
    void scanAndLoad();

    /// 全部驱动条目（含不可用的，UI 按 available/disabledReason 过滤展示）
    QList<DriverEntry> drivers() const { return m_entries; }

    /// 聚合全部可用驱动的在线设备（DeviceInfo.driverId 已填充）
    std::vector<ICanDevice::DeviceInfo> enumerateDevices() const;

    /// 工厂：按 driverId 创建设备实例（外置优先于内置；未知 id 返回 nullptr）
    /// 返回裸指针，调用方 unique_ptr 接管生命周期
    ICanDevice *createDevice(const QString &driverId, int subType) const;

    /// 外置驱动安装根目录（<应用目录>/drivers）
    static QString driversRootDir();

    /// 运行期卸载外置驱动：删除安装目录（尽力）并从条目表移除。
    /// 已加载的插件 DLL 进程内常驻不 unload（§7.4），重启后彻底清理。
    /// @return 错误信息，空串表示成功
    QString uninstallExternal(const QString &driverId);

    /// 启用/禁用驱动（方案 §7.4：禁用后设备树隐藏、不参与枚举/创建，
    /// 重启后不加载；持久化于 drivers/disabled.json）
    /// @return 是否找到条目并成功设置
    bool setDriverEnabled(const QString &driverId, bool enabled);

    // ---- Brand ↔ driverId 兼容映射（内置五家） ----
    static QString brandToDriverId(ICanDevice::Brand b);
    static ICanDevice::Brand driverIdToBrand(const QString &id);
    /// DeviceKind 兼容映射值（CanDeviceManager::DeviceKind，未知=-1）
    static int driverIdToDeviceKind(const QString &id);

signals:
    /// 驱动列表变化（安装/卸载/热加载后，设备树据此刷新）
    void driversChanged();

private:
    explicit DriverRegistry(QObject *parent = nullptr);

    void registerBuiltinDrivers();
    void scanExternalDrivers();
    bool loadExternal(const QString &dir, DriverEntry *outEntry, QString *errMsg);
    void upsertEntry(const DriverEntry &entry);
    /// 启动早期清理带 .uninstall 标记的目录（此时外置 DLL 尚未加载）
    void processPendingUninstalls();
    void loadDisabledFile();
    void saveDisabledFile();

    QList<DriverEntry> m_entries;
    QMap<QString, QPluginLoader *> m_loaders;    ///< 外置驱动 loader（进程内不 unload）
    QMap<QString, CanDriverPlugin *> m_plugins;  ///< 外置驱动实例
    QSet<QString> m_disabledIds;                 // 被禁用的 driverId（持久化 disabled.json）
    bool m_scanned = false;
};

#endif // DRIVERREGISTRY_H
