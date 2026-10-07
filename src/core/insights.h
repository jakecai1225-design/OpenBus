#ifndef INSIGHTS_H
#define INSIGHTS_H

#include <QObject>
#include <QElapsedTimer>
#include <QString>

class QNetworkAccessManager;

/**
 * Anonymous product telemetry to http://sin.org.cn/ob/api/event.
 * Disabled when telemetry.enabled is false. Never sends projects or CAN frames.
 */
class Insights : public QObject
{
    Q_OBJECT

public:
    static Insights *instance();

    void startSession();
    void endSession();
    void track(const QString &kind, const QString &id, int durationSec = 0);
    void track(const QString &kind, const QString &id, const QString &detail,
               int durationSec = 0);

private:
    explicit Insights(QObject *parent = nullptr);
    void post(const QString &kind, const QString &id, const QString &detail,
              int durationSec);
    QString visitorId();
    bool enabled() const;

    QNetworkAccessManager *m_nam = nullptr;
    QElapsedTimer m_session;
    bool m_sessionOpen = false;
};

#endif
