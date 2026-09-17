#include "pluginzmq.h"
#include "core/canframe.h"
#include "core/logging.h"

#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QMetaObject>

#include <zmq.hpp>

#include <chrono>
#include <cstring>
#include <vector>

namespace {

constexpr const char *kHostIdentity = "sin-host";
constexpr quint16 kProtoVersion = 1;
constexpr quint16 kMsgFrameBatch = 1;

void appendU16(QByteArray &b, quint16 v)
{
    const char le[2] = { char(v & 0xff), char((v >> 8) & 0xff) };
    b.append(le, 2);
}

void appendU32(QByteArray &b, quint32 v)
{
    const char le[4] = {
        char(v & 0xff), char((v >> 8) & 0xff),
        char((v >> 16) & 0xff), char((v >> 24) & 0xff)
    };
    b.append(le, 4);
}

void appendU64(QByteArray &b, quint64 v)
{
    for (int i = 0; i < 8; ++i)
        b.append(char((v >> (8 * i)) & 0xff));
}

} // namespace

// ---- I/O thread -----------------------------------------------------------

class PluginZmqHub::IoThread : public QThread
{
public:
    explicit IoThread(PluginZmqHub *hub) : m_hub(hub) {}

    void enqueue(OutMsg msg)
    {
        QMutexLocker lock(&m_mutex);
        m_out.enqueue(std::move(msg));
        m_cv.wakeOne();
    }

    void run() override
    {
        try {
            zmq::context_t ctx(1);
            zmq::socket_t router(ctx, zmq::socket_type::router);
            zmq::socket_t pub(ctx, zmq::socket_type::pub);

            router.set(zmq::sockopt::linger, 0);
            pub.set(zmq::sockopt::linger, 0);
            pub.set(zmq::sockopt::sndhwm, 1000);

            router.bind("tcp://127.0.0.1:*");
            pub.bind("tcp://127.0.0.1:*");

            const QString ctrlEp = QString::fromStdString(router.get(zmq::sockopt::last_endpoint));
            const QString dataEp = QString::fromStdString(pub.get(zmq::sockopt::last_endpoint));
            {
                QMutexLocker lock(&m_hub->m_endpointMutex);
                m_hub->m_ctrlEndpoint = ctrlEp;
                m_hub->m_dataEndpoint = dataEp;
            }

            spdlog::info("PluginZmqHub: ctrl={} data={}",
                         ctrlEp.toStdString(), dataEp.toStdString());

            m_hub->m_running.store(true);

            zmq::pollitem_t items[] = {
                { router.handle(), 0, ZMQ_POLLIN, 0 },
            };

            bool stop = false;
            while (!stop) {
                {
                    QMutexLocker lock(&m_mutex);
                    if (m_out.isEmpty())
                        m_cv.wait(&m_mutex, 20);
                }

                QList<OutMsg> batch;
                {
                    QMutexLocker lock(&m_mutex);
                    while (!m_out.isEmpty())
                        batch.append(m_out.dequeue());
                }

                for (const OutMsg &m : batch) {
                    if (m.kind == OutMsg::Stop) {
                        stop = true;
                        break;
                    }
                    if (m.kind == OutMsg::Ctrl) {
                        try {
                            router.send(zmq::buffer(kHostIdentity, std::strlen(kHostIdentity)),
                                        zmq::send_flags::sndmore | zmq::send_flags::dontwait);
                            router.send(zmq::buffer(m.payload.constData(), size_t(m.payload.size())),
                                        zmq::send_flags::dontwait);
                        } catch (const zmq::error_t &e) {
                            // Peer may not be connected yet — drop and retry later from app logic
                            spdlog::debug("PluginZmqHub: ctrl send deferred: {}", e.what());
                        }
                    } else if (m.kind == OutMsg::Frames) {
                        static const char topic[] = "frames";
                        pub.send(zmq::buffer(topic, sizeof(topic) - 1),
                                 zmq::send_flags::sndmore | zmq::send_flags::dontwait);
                        pub.send(zmq::buffer(m.payload.constData(), size_t(m.payload.size())),
                                 zmq::send_flags::dontwait);
                    }
                }

                if (stop)
                    break;

                // Block up to 20ms so inbound hello is not starved when idle
                zmq::poll(items, 1, std::chrono::milliseconds(20));
                while (items[0].revents & ZMQ_POLLIN) {
                    std::vector<zmq::message_t> parts;
                    // Capture more() BEFORE move — moved-from message_t is empty.
                    while (true) {
                        zmq::message_t part;
                        auto res = router.recv(part, zmq::recv_flags::dontwait);
                        if (!res)
                            break;
                        const bool more = part.more();
                        parts.push_back(std::move(part));
                        if (!more)
                            break;
                    }
                    if (parts.size() >= 2) {
                        const auto &body = parts.back();
                        QByteArray json(static_cast<const char *>(body.data()),
                                        int(body.size()));
                        spdlog::debug("PluginZmqHub: ctrl inbound {} bytes ({} parts)",
                                     json.size(), parts.size());
                        m_hub->pushInbox(json);
                    } else if (!parts.empty()) {
                        spdlog::warn("PluginZmqHub: ctrl frame dropped ({} parts)",
                                     parts.size());
                    }
                    items[0].revents = 0;
                    zmq::poll(items, 1, std::chrono::milliseconds(0));
                }
            }

            m_hub->m_running.store(false);
            router.close();
            pub.close();
            ctx.close();
        } catch (const zmq::error_t &e) {
            m_hub->m_running.store(false);
            const QString err = QStringLiteral("ZMQ error: %1").arg(e.what());
            spdlog::error("PluginZmqHub: {}", err.toStdString());
            m_hub->pushInbox(QByteArray()); // wake drain; hubError via dedicated path
            QMetaObject::invokeMethod(m_hub, [hub = m_hub, err]() {
                emit hub->hubError(err);
            }, Qt::QueuedConnection);
        }
    }

private:
    PluginZmqHub *m_hub = nullptr;
    QMutex m_mutex;
    QWaitCondition m_cv;
    QQueue<OutMsg> m_out;
};

// ---- PluginZmqHub ---------------------------------------------------------

PluginZmqHub::PluginZmqHub(QObject *parent)
    : QObject(parent)
{
    m_inboxTimer = new QTimer(this);
    m_inboxTimer->setInterval(10);
    connect(m_inboxTimer, &QTimer::timeout, this, &PluginZmqHub::drainInbox);
}

PluginZmqHub::~PluginZmqHub()
{
    stop();
}

void PluginZmqHub::pushInbox(const QByteArray &jsonUtf8)
{
    QMutexLocker lock(&m_inboxMutex);
    m_inbox.enqueue(jsonUtf8);
}

void PluginZmqHub::drainInbox()
{
    QList<QByteArray> batch;
    {
        QMutexLocker lock(&m_inboxMutex);
        while (!m_inbox.isEmpty())
            batch.append(m_inbox.dequeue());
    }
    for (const QByteArray &json : batch) {
        if (!json.isEmpty())
            emit ctrlMessageReceived(json);
    }
}

bool PluginZmqHub::start()
{
    if (m_io)
        return m_running.load();

    m_io = new IoThread(this);
    m_io->start();
    m_inboxTimer->start();

    for (int i = 0; i < 50; ++i) {
        if (m_running.load())
            break;
        QThread::msleep(20);
    }
    if (!m_running.load()) {
        spdlog::error("PluginZmqHub: failed to start I/O thread");
        stop();
        return false;
    }
    return true;
}

void PluginZmqHub::stop()
{
    if (m_inboxTimer)
        m_inboxTimer->stop();

    if (!m_io)
        return;

    m_io->enqueue(OutMsg{OutMsg::Stop, {}});
    if (!m_io->wait(3000)) {
        m_io->terminate();
        m_io->wait(1000);
    }
    delete m_io;
    m_io = nullptr;
    m_running.store(false);

    {
        QMutexLocker lock(&m_inboxMutex);
        m_inbox.clear();
    }
    QMutexLocker lock(&m_endpointMutex);
    m_ctrlEndpoint.clear();
    m_dataEndpoint.clear();
}

QString PluginZmqHub::ctrlEndpoint() const
{
    QMutexLocker lock(&m_endpointMutex);
    return m_ctrlEndpoint;
}

QString PluginZmqHub::dataEndpoint() const
{
    QMutexLocker lock(&m_endpointMutex);
    return m_dataEndpoint;
}

void PluginZmqHub::sendCtrl(const QByteArray &jsonUtf8)
{
    if (!m_io || !m_running.load())
        return;
    m_io->enqueue(OutMsg{OutMsg::Ctrl, jsonUtf8});
}

void PluginZmqHub::publishFrameBatch(const QList<CanFrame> &frames)
{
    if (!m_io || !m_running.load() || frames.isEmpty())
        return;
    m_io->enqueue(OutMsg{OutMsg::Frames, encodeFrameBatch(frames)});
}

QByteArray PluginZmqHub::encodeFrameBatch(const QList<CanFrame> &frames)
{
    QByteArray out;
    out.reserve(16 + frames.size() * 40);
    out.append("OBUS", 4);
    appendU16(out, kProtoVersion);
    appendU16(out, kMsgFrameBatch);
    appendU32(out, quint32(frames.size()));

    for (const CanFrame &f : frames) {
        appendU32(out, f.id);
        quint8 flags = 0;
        if (f.extended) flags |= 0x80;
        if (f.fd) flags |= 0x40;
        if (f.bitrateSwitch) flags |= 0x20;
        if (f.errorState) flags |= 0x10;
        if (f.direction == CanFrame::Tx) flags |= 0x08;
        out.append(char(flags));
        out.append(char(f.dlc));
        out.append(char(f.channel));
        const int n = qMin(f.data.size(), 64);
        out.append(char(n));
        quint64 tsNs = f.timestampNs;
        if (tsNs == 0 && f.timestamp > 0.0)
            tsNs = quint64(f.timestamp * 1e9);
        appendU64(out, tsNs);
        out.append(f.data.constData(), n);
    }
    return out;
}
