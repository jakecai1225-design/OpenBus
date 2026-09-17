#ifndef PLUGINZMQ_H
#define PLUGINZMQ_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <atomic>

struct CanFrame;
class QTimer;

/**
 * @brief Localhost ZMQ bridge for the plugin host.
 *
 * Control: ROUTER (this) <-> DEALER (Python) — JSON-RPC payloads.
 * Data:    PUB    (this) ->  SUB    (Python) — binary FRAME_BATCH.
 *
 * I/O runs on a dedicated thread; inbound control messages are queued and
 * delivered on the Qt GUI thread via a timer (avoids fragile cross-thread
 * QMetaObject::invokeMethod(functor) delivery).
 */
class PluginZmqHub : public QObject
{
    Q_OBJECT

public:
    explicit PluginZmqHub(QObject *parent = nullptr);
    ~PluginZmqHub() override;

    bool start();
    void stop();

    bool isRunning() const { return m_running.load(); }

    QString ctrlEndpoint() const;
    QString dataEndpoint() const;

    void sendCtrl(const QByteArray &jsonUtf8);
    void publishFrameBatch(const QList<CanFrame> &frames);

    static QByteArray encodeFrameBatch(const QList<CanFrame> &frames);

signals:
    void ctrlMessageReceived(const QByteArray &jsonUtf8);
    void hubError(const QString &error);

private slots:
    void drainInbox();

private:
    struct OutMsg {
        enum Kind { Ctrl, Frames, Stop } kind = Ctrl;
        QByteArray payload;
    };

    class IoThread;
    friend class IoThread;

    void pushInbox(const QByteArray &jsonUtf8);

    IoThread *m_io = nullptr;
    QTimer *m_inboxTimer = nullptr;

    mutable QMutex m_endpointMutex;
    QString m_ctrlEndpoint;
    QString m_dataEndpoint;

    QMutex m_inboxMutex;
    QQueue<QByteArray> m_inbox;

    std::atomic<bool> m_running{false};
};

#endif // PLUGINZMQ_H
