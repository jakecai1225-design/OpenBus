#include "marketindex.h"

#include "core/appconfig.h"
#include "core/logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// ============================================================
//  MarketIndex 实现 — market.json schema 2 加载 / 检索 / URL 解析
// ============================================================

MarketIndex *MarketIndex::instance()
{
    static MarketIndex s_index;
    return &s_index;
}

MarketIndex::MarketIndex(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    // 市场源优先级：设置项 market.url（插件系统方案 §六，可为
    // openbus_appstore 开发源）→ defaultMarketUrl()（本地/官方自动定位）
    const QString configured = AppConfig::instance()->getString(
        QStringLiteral("market.url"));
    m_marketUrl = !configured.isEmpty() ? QUrl(configured) : defaultMarketUrl();
}

QUrl MarketIndex::defaultMarketUrl()
{
    // 定位顺序：开发/发布 bin 布局（exe 在 build/bin 或 bin，市场在同级 market/）
    //   → 发布 exe 同级布局 → 官方占位
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(exeDir).absoluteFilePath(QStringLiteral("../market/market.json")),
        QDir(exeDir).absoluteFilePath(QStringLiteral("market/market.json")),
    };
    for (const auto &p : candidates) {
        if (QFileInfo::exists(p))
            return QUrl::fromLocalFile(QDir::cleanPath(p));
    }
    // 官方市场占位（发布后替换为实际 OSS 地址）
    return QUrl(QStringLiteral("https://market.openbus.local/market.json"));
}

void MarketIndex::refresh()
{
    QNetworkRequest req(m_marketUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { onReplyFinished(reply); });
}

void MarketIndex::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        OPENBUS_LOG_WARN("MarketIndex", "market.json fetch failed: {}",
                         m_lastError.toStdString());
        emit loaded(false, m_lastError);
        return;
    }
    const QByteArray data = reply->readAll();
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = QStringLiteral("market.json 解析失败: %1").arg(err.errorString());
        emit loaded(false, m_lastError);
        return;
    }
    const auto obj = doc.object();

    m_base = obj.value(QStringLiteral("base")).toString(QStringLiteral("."));
    m_updated = obj.value(QStringLiteral("updated")).toString();

    // ---- drivers[]：按驱动聚合（v2，方案 §13.4） ----
    m_drivers.clear();
    const auto drivers = obj.value(QStringLiteral("drivers")).toArray();
    for (const auto &v : drivers) {
        const auto d = v.toObject();
        DriverInfo info;
        info.id = d.value(QStringLiteral("id")).toString();
        info.name = d.value(QStringLiteral("name")).toString();
        info.vendor = d.value(QStringLiteral("vendor")).toString();
        info.version = d.value(QStringLiteral("version")).toString();
        info.package = d.value(QStringLiteral("package")).toString();
        info.sha256 = d.value(QStringLiteral("sha256")).toString();
        info.size = static_cast<qint64>(d.value(QStringLiteral("size")).toDouble());
        info.license = d.value(QStringLiteral("license")).toString();
        info.updatedAt = d.value(QStringLiteral("updatedAt")).toString();
        info.minAppVersion = d.value(QStringLiteral("minAppVersion")).toString();
        info.abiVersion = d.value(QStringLiteral("abiVersion")).toString();
        info.icon = d.value(QStringLiteral("icon")).toString();
        info.image = d.value(QStringLiteral("image")).toString();
        info.summary = d.value(QStringLiteral("summary")).toString();
        info.readme = d.value(QStringLiteral("readme")).toString();
        info.keywords = d.value(QStringLiteral("keywords")).toString();
        info.devices = d.value(QStringLiteral("devices")).toArray();
        if (!info.id.isEmpty() && !info.package.isEmpty())
            m_drivers.append(info);
    }

    // ---- plugins[]：Python 插件（.opk） ----
    m_plugins.clear();
    const auto plugins = obj.value(QStringLiteral("plugins")).toArray();
    for (const auto &v : plugins) {
        const auto p = v.toObject();
        PluginInfo info;
        info.id = p.value(QStringLiteral("id")).toString();
        info.name = p.value(QStringLiteral("name")).toString();
        info.publisher = p.value(QStringLiteral("publisher")).toString();
        info.version = p.value(QStringLiteral("version")).toString();
        info.description = p.value(QStringLiteral("description")).toString();
        info.icon = p.value(QStringLiteral("icon")).toString();
        info.package = p.value(QStringLiteral("package")).toString();
        info.sha256 = p.value(QStringLiteral("sha256")).toString();
        info.size = static_cast<qint64>(p.value(QStringLiteral("size")).toDouble());
        info.readme = p.value(QStringLiteral("readme")).toString();
        info.tags = p.value(QStringLiteral("tags")).toVariant().toStringList();
        info.keywords = p.value(QStringLiteral("keywords")).toString();
        info.minAppVersion = p.value(QStringLiteral("minAppVersion")).toString();
        info.updatedAt = p.value(QStringLiteral("updatedAt")).toString();
        if (!info.id.isEmpty() && !info.package.isEmpty())
            m_plugins.append(info);
    }

    m_loaded = true;
    m_lastError.clear();
    OPENBUS_LOG_INFO("MarketIndex", "market loaded: {} drivers / {} plugins",
                     m_drivers.size(), m_plugins.size());
    emit loaded(true, QString());
}

MarketIndex::DriverInfo MarketIndex::driverById(const QString &id) const
{
    for (const auto &d : m_drivers) {
        if (d.id == id)
            return d;
    }
    return DriverInfo();
}

MarketIndex::PluginInfo MarketIndex::pluginById(const QString &id) const
{
    for (const auto &p : m_plugins) {
        if (p.id == id)
            return p;
    }
    return PluginInfo();
}

bool MarketIndex::matchWords(const QString &text, const QStringList &fields)
{
    if (text.trimmed().isEmpty())
        return true;
    const auto words = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto &w : words) {
        bool hit = false;
        for (const auto &f : fields) {
            if (f.contains(w, Qt::CaseInsensitive)) {
                hit = true;
                break;
            }
        }
        if (!hit)
            return false;   // 多词 AND
    }
    return true;
}

QUrl MarketIndex::resolveUrl(const QString &relative) const
{
    if (relative.startsWith(QStringLiteral("http:"), Qt::CaseInsensitive)
        || relative.startsWith(QStringLiteral("https:"), Qt::CaseInsensitive)
        || relative.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        return QUrl(relative);

    // 相对路径：基于 market.json 的 URL（其目录 + base + relative）
    QUrl base = m_marketUrl.adjusted(QUrl::RemoveFilename);
    if (!m_base.isEmpty() && m_base != QLatin1String("."))
        base = base.resolved(QUrl(m_base));
    return base.resolved(QUrl(relative));
}
