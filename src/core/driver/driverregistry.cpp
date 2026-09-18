#include "driverregistry.h"
#include "candriverplugin.h"

#include "core/candevice_zlg.h"
#include "core/candevice_peak.h"
#include "core/candevice_kvaser.h"
#include "core/candevice_slcan.h"
#include "core/candevice_candle.h"
#include "core/candevicemanager.h"
#include "core/logging.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
#include <QVariant>
#include <QVersionNumber>

#include <array>
#include <algorithm>

// ============================================================
//  DriverRegistry 实现
// ============================================================

namespace {

/// 校验单个文件 sha256（ hex 字符串比较）
bool verifyFileSha256(const QString &path, const QString &expectedHex)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return false;
    return QString::fromLatin1(hash.result().toHex()).compare(expectedHex,
                                                              Qt::CaseInsensitive) == 0;
}

/// 解析 CHECKSUMS.sha256（格式与 plugin 包一致："<hex>  <相对路径>" 每行一条）
QHash<QString, QString> parseChecksums(const QString &path)
{
    QHash<QString, QString> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int sep = line.indexOf(QLatin1String("  "));
        if (sep <= 0)
            continue;
        result.insert(line.mid(sep + 2).trimmed(), line.left(sep).trimmed());
    }
    return result;
}

/// 从 devices[] 生成 JSON 数组的辅助
QJsonArray makeDevices(std::initializer_list<std::array<QVariant, 4>> rows)
{
    QJsonArray arr;
    for (const auto &r : rows) {
        QJsonObject o;
        o.insert(QStringLiteral("type"), r[0].toInt());
        o.insert(QStringLiteral("name"), r[1].toString());
        o.insert(QStringLiteral("channels"), r[2].toInt());
        o.insert(QStringLiteral("canFd"), r[3].toBool());
        arr.append(o);
    }
    return arr;
}

} // namespace

// ---- 单例 ----

DriverRegistry *DriverRegistry::instance()
{
    static DriverRegistry s_registry;
    return &s_registry;
}

DriverRegistry::DriverRegistry(QObject *parent)
    : QObject(parent)
{
    loadDisabledFile();          // 先读禁用清单（构造时即生效，内置/外置同规则）
    registerBuiltinDrivers();
}

// ---- 初始化与扫描 ----

void DriverRegistry::initialize()
{
    if (m_scanned)
        return;
    m_scanned = true;
    processPendingUninstalls();   // 清理上次运行期卸载的残留（此时外置 DLL 未加载）
    scanExternalDrivers();
}

void DriverRegistry::scanAndLoad()
{
    scanExternalDrivers();
    emit driversChanged();
}

void DriverRegistry::scanExternalDrivers()
{
    const QDir root(driversRootDir());
    const auto dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : dirs) {
        // 隐藏目录（driver_tool.py 的 .odp_* 临时目录等）：非驱动，跳过
        if (id.startsWith(QLatin1Char('.')))
            continue;
        const QString dirPath = root.filePath(id);

        // 待卸载目录：跳过（运行期卸载后残留，下次启动清理）
        if (QFile::exists(dirPath + QStringLiteral("/.uninstall")))
            continue;

        // 已加载的同 id 外置驱动：跳过（进程内不重复 load；重装走重启清理）
        if (m_plugins.contains(id))
            continue;

        // 被禁用的驱动：不加载（重启后保持禁用），仅登记条目供管理页启用
        if (m_disabledIds.contains(id)) {
            DriverEntry disabled;
            disabled.driverId = id;
            disabled.builtin = false;
            disabled.loaded = false;
            disabled.available = false;
            disabled.enabled = false;
            disabled.disabledReason = QStringLiteral("已禁用");
            disabled.installDir = dirPath;
            disabled.brand = driverIdToBrand(id);
            disabled.deviceKind = driverIdToDeviceKind(id);
            QFile jf(QDir(dirPath).filePath(QStringLiteral("driver.json")));
            if (jf.open(QIODevice::ReadOnly)) {
                const auto obj = QJsonDocument::fromJson(jf.readAll()).object();
                disabled.displayName = obj.value(QStringLiteral("name")).toString(id);
                disabled.version = obj.value(QStringLiteral("version")).toString();
                disabled.devices = obj.value(QStringLiteral("devices")).toArray();
            }
            upsertEntry(disabled);
            continue;
        }

        DriverEntry entry;
        QString errMsg;
        if (loadExternal(dirPath, &entry, &errMsg)) {
            upsertEntry(entry);
        } else {
            // Empty / incomplete drivers/<id>/ (common after partial builds)
            // must not replace a working builtin of the same id.
            bool keepBuiltin = false;
            for (const auto &e : m_entries) {
                if (e.driverId == id && e.builtin && e.available) {
                    keepBuiltin = true;
                    break;
                }
            }
            if (keepBuiltin) {
                OPENBUS_LOG_WARN("DriverRegistry",
                                 "driver '{}' external load failed ({}), "
                                 "keeping builtin",
                                 id.toStdString(), errMsg.toStdString());
                continue;
            }

            // Register (or update) as unavailable so the UI can show the reason
            DriverEntry failed;
            failed.driverId = id;
            failed.builtin = false;
            failed.loaded = false;
            failed.available = false;
            failed.disabledReason = errMsg;
            failed.installDir = dirPath;
            failed.brand = driverIdToBrand(id);
            failed.deviceKind = driverIdToDeviceKind(id);
            QFile jf(QDir(dirPath).filePath(QStringLiteral("driver.json")));
            if (jf.open(QIODevice::ReadOnly)) {
                const auto doc = QJsonDocument::fromJson(jf.readAll());
                const auto obj = doc.object();
                failed.displayName = obj.value(QStringLiteral("name")).toString(id);
                failed.version = obj.value(QStringLiteral("version")).toString();
                failed.devices = obj.value(QStringLiteral("devices")).toArray();
            }
            if (failed.displayName.isEmpty())
                failed.displayName = id;
            upsertEntry(failed);
            OPENBUS_LOG_WARN("DriverRegistry",
                             "driver '{}' load failed: {}", id.toStdString(),
                             errMsg.toStdString());
        }
    }
}

bool DriverRegistry::loadExternal(const QString &dir, DriverEntry *outEntry,
                                  QString *errMsg)
{
    const QDir d(dir);

    // 1. driver.json 清单
    QFile jf(d.filePath(QStringLiteral("driver.json")));
    if (!jf.open(QIODevice::ReadOnly)) {
        *errMsg = QStringLiteral("缺少 driver.json");
        return false;
    }
    const auto doc = QJsonDocument::fromJson(jf.readAll());
    if (!doc.isObject()) {
        *errMsg = QStringLiteral("driver.json 格式错误");
        return false;
    }
    const auto obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString().toLower();
    if (id.isEmpty() || id != d.dirName().toLower()) {
        *errMsg = QStringLiteral("driver.json id 与目录名不一致");
        return false;
    }

    // 2. sha256 完整性校验（CHECKSUMS.sha256 存在时强制）
    const QString sumsPath = d.filePath(QStringLiteral("CHECKSUMS.sha256"));
    if (QFile::exists(sumsPath)) {
        const auto sums = parseChecksums(sumsPath);
        for (auto it = sums.constBegin(); it != sums.constEnd(); ++it) {
            const QString rel = it.key();
            if (!verifyFileSha256(d.filePath(rel), it.value())) {
                *errMsg = QStringLiteral("校验失败: %1").arg(rel);
                return false;
            }
        }
    }

    // 3. 定位插件 DLL（driver_<id>.dll）
    const QString dllName = QStringLiteral("driver_%1.dll").arg(id);
    const QString dllPath = d.filePath(dllName);
    if (!QFile::exists(dllPath)) {
        *errMsg = QStringLiteral("缺少插件 %1").arg(dllName);
        return false;
    }

    // 4. metaData 预检（IID 版本；不满足直接拒绝，不进入 load()）
    QPluginLoader *loader = new QPluginLoader(dllPath, this);
    const auto meta = loader->metaData();
    const QString iid = meta.value(QStringLiteral("IID")).toString();
    if (iid != QLatin1String("com.sin.openbus.CanDriverPlugin/1.0")) {
        *errMsg = QStringLiteral("接口版本不匹配 (IID=%1)").arg(iid);
        loader->deleteLater();
        return false;
    }

    // 5. 加载 + 接口转换
    if (!loader->load()) {
        *errMsg = QStringLiteral("加载失败: %1").arg(loader->errorString());
        loader->deleteLater();
        return false;
    }
    auto *plugin = qobject_cast<CanDriverPlugin *>(loader->instance());
    if (!plugin) {
        *errMsg = QStringLiteral("未实现 CanDriverPlugin 接口");
        loader->unload();   // 未成功注册的 loader 可以安全卸载
        loader->deleteLater();
        return false;
    }

    // 6. 注册（loader 进程内常驻，不 unload）
    m_loaders.insert(id, loader);
    m_plugins.insert(id, plugin);

    outEntry->driverId = id;
    outEntry->displayName = plugin->displayName();
    if (outEntry->displayName.isEmpty())
        outEntry->displayName = obj.value(QStringLiteral("name")).toString(id);
    outEntry->version = plugin->version();
    outEntry->iconPath = d.filePath(QStringLiteral("icon.svg"));
    outEntry->builtin = false;
    outEntry->loaded = true;
    outEntry->available = true;
    outEntry->devices = obj.value(QStringLiteral("devices")).toArray();
    outEntry->brand = driverIdToBrand(id);
    outEntry->deviceKind = driverIdToDeviceKind(id);
    outEntry->installDir = dir;
    return true;
}

void DriverRegistry::upsertEntry(const DriverEntry &entry)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        DriverEntry &existing = m_entries[i];
        if (existing.driverId != entry.driverId)
            continue;

        // 同 id：外置可用时仅当版本 >= 内置/旧外置才覆盖（方案 §四要点 2）
        if (entry.available) {
            const auto oldVer = QVersionNumber::fromString(existing.version);
            const auto newVer = QVersionNumber::fromString(entry.version);
            if (existing.available && QVersionNumber::compare(newVer, oldVer) < 0) {
                OPENBUS_LOG_INFO("DriverRegistry",
                                 "driver '{}' v{} ignored (installed v{} is newer)",
                                 entry.driverId.toStdString(),
                                 newVer.toString().toStdString(),
                                 oldVer.toString().toStdString());
                return;
            }
            m_entries[i] = entry;
            return;
        }
        // 不可用条目：仅在没有可用条目时登记
        if (!existing.available)
            m_entries[i] = entry;
        return;
    }
    m_entries.append(entry);
}

// ---- 内置驱动注册 ----

void DriverRegistry::registerBuiltinDrivers()
{
    // Open-source backends (candle / slcan) and vendor backends that still
    // ship linked into openbus_data (ZLG / PEAK / Kvaser) register here.
    // Matching external .odp under drivers/<id>/ overrides via upsertEntry
    // when load succeeds; a broken/empty drivers/<id>/ must NOT wipe the
    // builtin (see scanExternalDrivers).

    auto addBuiltin = [this](const QString &id, const QString &name,
                             ICanDevice::Brand brand, int kind, bool available,
                             const QString &reason = {}) {
        DriverEntry e;
        e.driverId = id;
        e.displayName = name;
        e.version = QStringLiteral("1.0.0");
        e.builtin = true;
        e.loaded = true;
        e.available = available;
        e.disabledReason = reason;
        e.enabled = !m_disabledIds.contains(id);
        e.brand = brand;
        e.deviceKind = kind;
        m_entries.append(e);
    };

    const bool zlgOk = CanDeviceZLG::isAvailable();
    addBuiltin(QStringLiteral("zlg"), QStringLiteral("ZLG"),
               ICanDevice::Brand::ZLG,
               static_cast<int>(CanDeviceManager::DeviceKind::ZLG),
               zlgOk,
               zlgOk ? QString()
                     : QStringLiteral("zlgcan.dll not found (place next to openbus.exe or in drivers/zlg/vendor)"));

    const bool peakOk = CanDevicePEAK::isAvailable();
    addBuiltin(QStringLiteral("peak"), QStringLiteral("PEAK PCAN"),
               ICanDevice::Brand::PEAK,
               static_cast<int>(CanDeviceManager::DeviceKind::PEAK),
               peakOk,
               peakOk ? QString()
                      : QStringLiteral("PCANBasic.dll not found (drivers/peak/vendor)"));

    const bool kvaserOk = CanDeviceKvaser::isAvailable();
    addBuiltin(QStringLiteral("kvaser"), QStringLiteral("Kvaser"),
               ICanDevice::Brand::Kvaser,
               static_cast<int>(CanDeviceManager::DeviceKind::Kvaser),
               kvaserOk,
               kvaserOk ? QString()
                        : QStringLiteral("canlib32.dll not found (drivers/kvaser/vendor)"));

    const bool candleOk = CanDeviceCandle::isAvailable();
    addBuiltin(QStringLiteral("candle"), QStringLiteral("Candle / GS_USB"),
               ICanDevice::Brand::Candle,
               static_cast<int>(CanDeviceManager::DeviceKind::Candle),
               candleOk,
               candleOk ? QString()
                        : QStringLiteral("libusb-1.0.dll not found (drivers/candle/vendor)"));

    addBuiltin(QStringLiteral("slcan"), QStringLiteral("SLCAN"),
               ICanDevice::Brand::SLCAN,
               static_cast<int>(CanDeviceManager::DeviceKind::SLCAN),
               true);
}

// ---- 聚合枚举与工厂 ----

std::vector<ICanDevice::DeviceInfo> DriverRegistry::enumerateDevices() const
{
    std::vector<ICanDevice::DeviceInfo> list;
    for (const auto &e : m_entries) {
        if (!e.available || !e.enabled)
            continue;

        // 外置驱动：插件实例枚举（DLL 内部 QLibrary 加载厂商 SDK）
        if (!e.builtin) {
            const auto *plugin = m_plugins.value(e.driverId);
            if (!plugin)
                continue;
            auto devs = plugin->enumerateDevices();
            list.reserve(list.size() + devs.size());
            for (auto &d : devs) {
                d.driverId = e.driverId;
                // move 转移（DEF-06 复现缓解）：逐元素深拷贝需解引用插件侧
                // 堆字符串（源对象存在被厂商线程踩的风险），move 仅搬指针
                list.push_back(std::move(d));
            }
            continue;
        }

        // 内置驱动：静态后端枚举
        std::vector<ICanDevice::DeviceInfo> devs;
        switch (e.brand) {
        case ICanDevice::Brand::ZLG:
            devs = CanDeviceZLG::enumerate();
            break;
        case ICanDevice::Brand::PEAK:
            devs = CanDevicePEAK::enumerate();
            break;
        case ICanDevice::Brand::Kvaser:
            devs = CanDeviceKvaser::enumerate();
            break;
        case ICanDevice::Brand::Candle:
            devs = CanDeviceCandle::enumerate();
            break;
        case ICanDevice::Brand::SLCAN:
            devs = CanDeviceSlcan::enumerate();
            break;
        default:
            break;
        }
        for (auto &d : devs) {
            d.driverId = e.driverId;
            list.push_back(d);
        }
    }
    return list;
}

ICanDevice *DriverRegistry::createDevice(const QString &driverId, int subType) const
{
    // 被禁用的驱动不参与创建
    for (const auto &e : m_entries) {
        if (e.driverId == driverId && !e.enabled)
            return nullptr;
    }

    // 外置优先
    const auto *plugin = m_plugins.value(driverId, nullptr);
    if (plugin)
        return plugin->createDevice(subType);

    // 内置后端
    const ICanDevice::Brand brand = driverIdToBrand(driverId);
    switch (brand) {
    case ICanDevice::Brand::ZLG:
        return new CanDeviceZLG(static_cast<CanDeviceZLG::DeviceType>(subType));
    case ICanDevice::Brand::PEAK:
        return new CanDevicePEAK(subType);
    case ICanDevice::Brand::Kvaser:
        return new CanDeviceKvaser(subType);
    case ICanDevice::Brand::Candle:
        return new CanDeviceCandle(subType);
    case ICanDevice::Brand::SLCAN:
        return new CanDeviceSlcan(subType);
    default:
        return nullptr;
    }
}

// ---- 路径与映射 ----

QString DriverRegistry::uninstallExternal(const QString &driverId)
{
    // 只允许卸载外置驱动
    for (const auto &e : m_entries) {
        if (e.driverId == driverId && e.builtin)
            return QStringLiteral("内置驱动不可卸载");
    }

    const QString dir = QDir(driversRootDir()).filePath(driverId);
    if (!QDir(dir).exists())
        return QStringLiteral("驱动目录不存在: %1").arg(dir);

    // 写卸载标记（DLL 可能被占用，剩余文件下次启动清理）
    QFile marker(dir + QStringLiteral("/.uninstall"));
    if (marker.open(QIODevice::WriteOnly)) {
        marker.write("pending\n");
        marker.close();
    }
    QDir(dir).removeRecursively();   // 尽力删除（占用文件失败忽略）

    // 从条目表移除（m_plugins/m_loaders 保留引用防野指针，进程退出时释放）
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].driverId == driverId && !m_entries[i].builtin) {
            m_entries.removeAt(i);
            break;
        }
    }
    emit driversChanged();
    OPENBUS_LOG_INFO("DriverRegistry", "driver '{}' uninstalled",
                     driverId.toStdString());
    return QString();
}

void DriverRegistry::processPendingUninstalls()
{
    const QDir root(driversRootDir());
    const auto dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : dirs) {
        if (id.startsWith(QLatin1Char('.')))
            continue;
        const QString dirPath = root.filePath(id);
        if (QFile::exists(dirPath + QStringLiteral("/.uninstall"))) {
            if (QDir(dirPath).removeRecursively()) {
                OPENBUS_LOG_INFO("DriverRegistry",
                                 "cleaned pending uninstall: '{}'",
                                 id.toStdString());
            }
        }
    }
}

// ---- 禁用机制（方案 §7.4） ----

bool DriverRegistry::setDriverEnabled(const QString &driverId, bool enabled)
{
    const QString id = driverId.toLower();
    bool found = false;
    for (auto &e : m_entries) {
        if (e.driverId != id)
            continue;
        e.enabled = enabled;
        // 禁用时记录原因；重新启用时清空（不可用原因由 available 逻辑重建）
        if (!enabled)
            e.disabledReason = QStringLiteral("已禁用");
        else if (e.disabledReason == QLatin1String("已禁用"))
            e.disabledReason.clear();
        found = true;
    }
    if (!found)
        return false;

    if (enabled)
        m_disabledIds.remove(id);
    else
        m_disabledIds.insert(id);
    saveDisabledFile();
    emit driversChanged();
    OPENBUS_LOG_INFO("DriverRegistry", "driver '{}' {}",
                     id.toStdString(), enabled ? "enabled" : "disabled");
    return true;
}

void DriverRegistry::loadDisabledFile()
{
    m_disabledIds.clear();
    QFile f(QDir(driversRootDir()).filePath(QStringLiteral("disabled.json")));
    if (!f.open(QIODevice::ReadOnly))
        return;   // 无文件 = 全部启用
    const auto arr = QJsonDocument::fromJson(f.readAll())
                         .object()
                         .value(QStringLiteral("disabled"))
                         .toArray();
    for (const auto &v : arr)
        m_disabledIds.insert(v.toString().toLower());
}

void DriverRegistry::saveDisabledFile()
{
    QDir(driversRootDir()).mkpath(QStringLiteral("."));
    QJsonArray arr;
    auto ids = m_disabledIds.values();
    std::sort(ids.begin(), ids.end());
    for (const auto &id : ids)
        arr.append(id);
    QJsonObject obj;
    obj.insert(QStringLiteral("disabled"), arr);

    QFile f(QDir(driversRootDir()).filePath(QStringLiteral("disabled.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        OPENBUS_LOG_WARN("DriverRegistry", "disabled.json 写入失败");
        return;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QString DriverRegistry::driversRootDir()
{
    return QCoreApplication::applicationDirPath()
           + QStringLiteral("/drivers");
}

QString DriverRegistry::brandToDriverId(ICanDevice::Brand b)
{
    switch (b) {
    case ICanDevice::Brand::ZLG:      return QStringLiteral("zlg");
    case ICanDevice::Brand::PEAK:     return QStringLiteral("peak");
    case ICanDevice::Brand::Kvaser:   return QStringLiteral("kvaser");
    case ICanDevice::Brand::TongXing: return QStringLiteral("tongxing");
    case ICanDevice::Brand::SLCAN:    return QStringLiteral("slcan");
    case ICanDevice::Brand::Candle:   return QStringLiteral("candle");
    }
    return QString();
}

ICanDevice::Brand DriverRegistry::driverIdToBrand(const QString &id)
{
    const QString key = id.toLower();
    if (key == QLatin1String("zlg"))      return ICanDevice::Brand::ZLG;
    if (key == QLatin1String("peak"))     return ICanDevice::Brand::PEAK;
    if (key == QLatin1String("kvaser"))   return ICanDevice::Brand::Kvaser;
    if (key == QLatin1String("tongxing")) return ICanDevice::Brand::TongXing;
    if (key == QLatin1String("slcan"))    return ICanDevice::Brand::SLCAN;
    if (key == QLatin1String("candle"))   return ICanDevice::Brand::Candle;
    return ICanDevice::Brand::SLCAN;   // 长尾驱动：无 Brand 枚举，仅 driverId 标识
}

int DriverRegistry::driverIdToDeviceKind(const QString &id)
{
    const QString key = id.toLower();
    if (key == QLatin1String("zlg"))      return static_cast<int>(CanDeviceManager::DeviceKind::ZLG);
    if (key == QLatin1String("peak"))     return static_cast<int>(CanDeviceManager::DeviceKind::PEAK);
    if (key == QLatin1String("kvaser"))   return static_cast<int>(CanDeviceManager::DeviceKind::Kvaser);
    if (key == QLatin1String("tongxing")) return static_cast<int>(CanDeviceManager::DeviceKind::TongXing);
    if (key == QLatin1String("slcan"))    return static_cast<int>(CanDeviceManager::DeviceKind::SLCAN);
    if (key == QLatin1String("candle"))   return static_cast<int>(CanDeviceManager::DeviceKind::Candle);
    return -1;   // 长尾驱动：DeviceConnectionTab 需声明式参数 schema（阶段 5）
}
