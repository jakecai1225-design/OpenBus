#ifndef PLUGINHOST_H
#define PLUGINHOST_H

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <functional>

class QProcess;
class QProcessEnvironment;
class QTimer;
class PluginZmqHub;

/**
 * @brief Python plugin host process — QProcess lifecycle + ZMQ JSON-RPC control.
 *
 * Control plane: PluginZmqHub ROUTER/DEALER (JSON-RPC 2.0).
 * Data plane:    PluginZmqHub PUB (binary frames) — see PluginManager flush.
 */
class PluginHost : public QObject
{
    Q_OBJECT

public:
    explicit PluginHost(QObject *parent = nullptr);
    ~PluginHost();

    /// Start ZMQ hub then launch Python host (sin_host.py).
    bool start(const QString &pythonExe, const QString &hostScript,
               const QString &sdkDir, const QString &pluginsDir);

    void stop();
    bool isRunning() const;
    /// Process alive (may still be waiting for host.hello).
    bool isProcessAlive() const;
    qint64 processId() const;

    /// Restart process using cached start() arguments (no-op if already alive).
    bool ensureProcess();

    PluginZmqHub *zmqHub() const { return m_zmq; }

    void sendNotification(const QString &method, const QJsonObject &params = {});
    void sendRequest(const QString &method, const QJsonObject &params,
                     std::function<void(const QJsonValue &)> callback);
    void sendRequest(const QString &method, const QJsonObject &params = {});
    void sendResponse(const QJsonValue &id, const QJsonValue &result);
    void sendErrorResponse(const QJsonValue &id, int code, const QString &message);

signals:
    void messageReceived(const QString &method, const QJsonObject &params, const QJsonValue &id);
    void hostStarted();
    void hostCrashed();
    void hostError(const QString &error);

private slots:
    void onCtrlMessage(const QByteArray &jsonUtf8);
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError(int error);
    void onRestartTimer();
    void onHelloTimeout();

private:
    QProcess *m_process = nullptr;
    PluginZmqHub *m_zmq = nullptr;
    int m_nextRequestId = 1;
    QHash<int, std::function<void(const QJsonValue &)>> m_pendingRequests;
    QTimer *m_restartTimer = nullptr;
    QTimer *m_helloTimer = nullptr;
    int m_restartAttempts = 0;
    bool m_helloSeen = false;

    QString m_pythonExe;
    QString m_hostScript;
    QString m_sdkDir;
    QString m_pluginsDir;

    void sendMessage(const QJsonObject &msg);
    void handleJsonObject(const QJsonObject &obj);
    void scheduleRestart();
    void prepareHostEnvironment(QProcessEnvironment &env) const;

    static constexpr int kStopped = -1;
};

#endif // PLUGINHOST_H
