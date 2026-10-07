#include "insights.h"
#include "appconfig.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUuid>

Insights *Insights::instance()
{
    static Insights inst;
    return &inst;
}

Insights::Insights(QObject *parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

bool Insights::enabled() const
{
    return AppConfig::instance()->getBool(QStringLiteral("telemetry.enabled"), true);
}

QString Insights::visitorId()
{
    AppConfig *cfg = AppConfig::instance();
    QString id = cfg->getString(QStringLiteral("telemetry.installId"));
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        cfg->set(QStringLiteral("telemetry.installId"), id);
        cfg->save();
    }
    return id;
}

void Insights::startSession()
{
    if (!enabled())
        return;
    m_session.start();
    m_sessionOpen = true;
    post(QStringLiteral("session"), QStringLiteral("start"), QString(), 0);
}

void Insights::endSession()
{
    if (!m_sessionOpen)
        return;
    m_sessionOpen = false;
    const int sec = static_cast<int>(m_session.elapsed() / 1000);
    post(QStringLiteral("session"), QStringLiteral("end"), QString(), sec);
}

void Insights::track(const QString &kind, const QString &id, int durationSec)
{
    track(kind, id, QString(), durationSec);
}

void Insights::track(const QString &kind, const QString &id, const QString &detail,
                     int durationSec)
{
    if (!enabled())
        return;
    post(kind, id, detail, durationSec);
}

void Insights::post(const QString &kind, const QString &id, const QString &detail,
                    int durationSec)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), kind);
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("source"), QStringLiteral("desktop"));
    obj.insert(QStringLiteral("visitor"), visitorId());
    obj.insert(QStringLiteral("action"), kind == QStringLiteral("session") ? id : QString());
    obj.insert(QStringLiteral("durationSec"), durationSec);
    obj.insert(QStringLiteral("appVersion"), QCoreApplication::applicationVersion());
    if (!detail.isEmpty()) {
        if (kind == QStringLiteral("search"))
            obj.insert(QStringLiteral("q"), detail.left(80));
        else
            obj.insert(QStringLiteral("q"), detail.left(80));
    }

    QNetworkRequest req(QUrl(QStringLiteral("http://sin.org.cn/ob/api/event")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(obj).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}
