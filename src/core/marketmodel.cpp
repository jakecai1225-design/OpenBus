#include "core/marketmodel.h"

#include "core/driver/driverregistry.h"
#include "core/driver/marketindex.h"
#include "core/plugin/plugininfo.h"
#include "core/plugin/pluginmanager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QStandardPaths>
#include <QSvgRenderer>

// ============================================================
//  PluginUi::pluginIconPixmap — 图标加载 + 首字母头像兜底
//  （迁移自 plugindetailpage.cpp，方案 §13.10）
// ============================================================

QPixmap PluginUi::pluginIconPixmap(const QString &iconPath, const QString &name, int size)
{
    if (!iconPath.isEmpty()) {
        if (iconPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
            // SVG: render via QSvgRenderer (no imageformats plugin), scale by DPR
            QFile f(iconPath);
            if (f.open(QIODevice::ReadOnly)) {
                QSvgRenderer renderer(f.readAll());
                if (renderer.isValid()) {
                    const qreal dpr = qApp->devicePixelRatio();
                    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
                    pm.setDevicePixelRatio(dpr);
                    pm.fill(Qt::transparent);
                    QPainter pt(&pm);
                    pt.setRenderHint(QPainter::Antialiasing);
                    renderer.render(&pt);
                    return pm;
                }
            }
        } else {
            QIcon icon(iconPath);
            if (!icon.isNull())
                return icon.pixmap(size, size);
        }
    }

    // Colored rounded badge: 1–3 letter mark (unified driver + plugin look)
    static const QColor palette[] = {
        QColor("#4ec9b0"), QColor("#569cd6"), QColor("#c586c0"), QColor("#dcdcaa"),
        QColor("#ce9178"), QColor("#6a9955"), QColor("#d7ba7d"), QColor("#9cdcfe"),
    };
    const int n = sizeof(palette) / sizeof(palette[0]);
    const QColor color = palette[int(qHash(name) % n)];

    QString label = name.trimmed();
    if (label.isEmpty()) {
        label = QStringLiteral("?");
    } else {
        // Prefer a short ASCII token already prepared by the caller (e.g. "ZLG",
        // "DSH"). Otherwise keep a single grapheme for long display titles.
        bool asciiToken = true;
        for (const QChar &c : label) {
            if (!(c.isLetterOrNumber() && c.unicode() < 128)) {
                asciiToken = false;
                break;
            }
        }
        if (asciiToken && label.size() <= 4) {
            label = label.toUpper();
        } else {
            label = QString(label.at(0)).toUpper();
        }
    }

    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, size, size, size * 0.22, size * 0.22);

    QFont f = p.font();
    f.setBold(true);
    const int len = label.size();
    const double ratio = (len >= 3) ? 0.30 : (len == 2) ? 0.40 : 0.50;
    f.setPixelSize(qMax(9, int(size * ratio)));
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(pm.rect(), Qt::AlignCenter, label);
    return pm;
}

// ============================================================
//  MarketModel — 四源聚合为行条目（市场页与侧边栏迷你市场共用）
// ============================================================

namespace {

/// 已装驱动版本查询（不存在返回空）
QString localDriverVersion(const QString &driverId)
{
    for (const auto &e : DriverRegistry::instance()->drivers()) {
        if (e.driverId == driverId)
            return e.version;
    }
    return QString();
}

/// 已装插件版本查询（不存在返回空）
QString localPluginVersion(const QString &id)
{
    const auto plugins = PluginManager::instance()->discoveredPlugins();
    for (const auto &p : plugins) {
        if (p.name == id)
            return p.version;
    }
    return QString();
}

} // namespace

QVector<MarketEntryData> MarketModel::collectInstalledDrivers()
{
    QVector<MarketEntryData> out;
    const auto entries = DriverRegistry::instance()->drivers();
    for (const auto &e : entries) {
        MarketEntryData d;
        d.item = { MarketItem::InstalledDriver, e.driverId };
        d.title = e.displayName;
        const QString source = e.builtin ? QStringLiteral("内置")
                                         : QStringLiteral("外置");
        const QString state = e.enabled
                                  ? (e.available ? QStringLiteral("可用")
                                                 : QStringLiteral("不可用"))
                                  : QStringLiteral("已禁用");
        const QString version = e.version.isEmpty()
                                    ? QStringLiteral("-") : e.version;
        d.meta = QStringLiteral("v%1 · %2 · %3").arg(version, source, state);
        d.status = e.enabled ? QString() : QStringLiteral("已禁用");
        d.marketIcon = MarketIndex::instance()->driverById(e.driverId).icon;
        d.searchFields = { e.displayName, e.driverId, e.version };
        out.append(d);
    }
    return out;
}

QVector<MarketEntryData> MarketModel::collectInstalledPlugins()
{
    QVector<MarketEntryData> out;
    auto *pm = PluginManager::instance();
    const auto plugins = pm->discoveredPlugins();
    for (const auto &p : plugins) {
        MarketEntryData d;
        d.item = { MarketItem::InstalledPlugin, p.name };
        d.title = p.name;
        d.meta = QStringLiteral("v%1 · %2").arg(p.version, p.author);
        const bool enabled = pm->isPluginEnabled(p.name);
        const bool activated = pm->isPluginActivated(p.name);
        d.status = !enabled ? QStringLiteral("已禁用")
                  : activated ? QStringLiteral("运行中")
                              : QStringLiteral("已就绪");
        d.marketIcon = MarketIndex::instance()->pluginById(p.name).icon;
        d.searchFields = { p.name, p.version, p.author, p.description };
        out.append(d);
    }
    return out;
}

QVector<MarketEntryData> MarketModel::collectMarketDrivers()
{
    QVector<MarketEntryData> out;
    for (const auto &drv : MarketIndex::instance()->drivers()) {
        MarketEntryData d;
        d.item = { MarketItem::MarketDriver, drv.id };
        d.title = drv.name;
        d.meta = drv.summary;
        const QString local = localDriverVersion(drv.id);
        d.status = local.isEmpty() ? QStringLiteral("未安装")
                                   : QStringLiteral("已安装 v%1").arg(local);
        d.marketIcon = drv.icon;
        d.searchFields = { drv.name, drv.vendor, drv.summary, drv.keywords, drv.id };
        for (const auto &v : drv.devices)
            d.searchFields << v.toObject().value(QStringLiteral("model")).toString();
        out.append(d);
    }
    return out;
}

QVector<MarketEntryData> MarketModel::collectMarketPlugins()
{
    QVector<MarketEntryData> out;
    for (const auto &p : MarketIndex::instance()->plugins()) {
        MarketEntryData d;
        d.item = { MarketItem::MarketPlugin, p.id };
        d.title = p.name;
        d.meta = p.description;
        d.status = localPluginVersion(p.id).isEmpty() ? QStringLiteral("未安装")
                                                      : QStringLiteral("已安装");
        d.marketIcon = p.icon;
        d.searchFields = { p.name, p.id, p.publisher, p.description,
                           p.keywords, p.tags.join(QLatin1Char(' ')) };
        out.append(d);
    }
    return out;
}

QPixmap MarketModel::pluginIconLocal(const QString &name)
{
    const auto plugins = PluginManager::instance()->discoveredPlugins();
    for (const auto &p : plugins) {
        if (p.name != name)
            continue;
        const QString f = p.iconFilePath();
        if (!f.isEmpty())
            return QPixmap(f);
        break;
    }
    return QPixmap();
}

// ============================================================
//  市场图标/图片：磁盘缓存 + 网络异步（迁移自 MarketTab，两视图共用）
// ============================================================

namespace {

QString imageCachePath(const QUrl &url)
{
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(
        url.toString().toUtf8(), QCryptographicHash::Sha1).toHex());
    QString suffix = QFileInfo(url.path()).suffix();
    if (suffix.isEmpty())
        suffix = QStringLiteral("img");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/market-cache");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + hash + QLatin1Char('.') + suffix;
}

} // namespace

void MarketModel::fetchMarketPixmap(const QUrl &url,
                                    const std::function<void(const QPixmap &)> &cb)
{
    if (!url.isValid())
        return;

    // 磁盘缓存命中 → 直接回调
    const QString cache = imageCachePath(url);
    QPixmap pm(cache);
    if (!pm.isNull()) {
        cb(pm);
        return;
    }

    static QNetworkAccessManager *nam = new QNetworkAccessManager;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cache, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray data = reply->readAll();
        QPixmap img;
        if (!img.loadFromData(data))
            return;
        // 写入磁盘缓存（失败不影响显示）
        QFile f(cache);
        if (f.open(QIODevice::WriteOnly))
            f.write(data);
        cb(img);
    });
}
