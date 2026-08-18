#include "marketindex.h"

#include "core/logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// ============================================================
//  MarketIndex 实现 — market.json 加载 / 检索 / URL 解析
// ============================================================

namespace {
bool matchWords(const QString &text, const QStringList &fields)
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
} // namespace

MarketIndex *MarketIndex::instance()
{
    static MarketIndex s_index;
    return &s_index;
}

MarketIndex::MarketIndex(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_marketUrl = defaultMarketUrl();
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
        if (!info.id.isEmpty() && !info.package.isEmpty())
            m_drivers.append(info);
    }

    m_devices.clear();
    const auto devices = obj.value(QStringLiteral("devices")).toArray();
    for (const auto &v : devices) {
        const auto d = v.toObject();
        DeviceInfo info;
        info.driverId = d.value(QStringLiteral("driverId")).toString();
        info.model = d.value(QStringLiteral("model")).toString();
        info.vendor = d.value(QStringLiteral("vendor")).toString();
        info.type = d.value(QStringLiteral("type")).toInt();
        info.summary = d.value(QStringLiteral("summary")).toString();
        info.tags = d.value(QStringLiteral("tags")).toVariant().toStringList();
        info.images = d.value(QStringLiteral("images")).toVariant().toStringList();
        info.intro = d.value(QStringLiteral("intro")).toString();
        info.specs = d.value(QStringLiteral("specs")).toObject();
        info.keywords = d.value(QStringLiteral("keywords")).toString();
        if (!info.model.isEmpty())
            m_devices.append(info);
    }

    m_loaded = true;
    m_lastError.clear();
    OPENBUS_LOG_INFO("MarketIndex", "market loaded: {} drivers / {} devices",
                     m_drivers.size(), m_devices.size());
    emit loaded(true, QString());
}

QList<MarketIndex::DeviceInfo> MarketIndex::search(const QString &text) const
{
    if (text.trimmed().isEmpty())
        return m_devices;
    QList<DeviceInfo> result;
    for (const auto &d : m_devices) {
        QStringList fields{ d.model, d.vendor, d.summary, d.keywords, d.tags.join(QLatin1Char(' ')) };
        if (matchWords(text, fields))
            result.append(d);
    }
    return result;
}

MarketIndex::DriverInfo MarketIndex::driverById(const QString &id) const
{
    for (const auto &d : m_drivers) {
        if (d.id == id)
            return d;
    }
    return DriverInfo();
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
