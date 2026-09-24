#include "pluginhost.h"
#include "pluginzmq.h"
#include "core/logging.h"
#include "core/appconfig.h"

#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>

PluginHost::PluginHost(QObject *parent)
    : QObject(parent)
{
    m_restartTimer = new QTimer(this);
    m_restartTimer->setSingleShot(true);
    connect(m_restartTimer, &QTimer::timeout, this, &PluginHost::onRestartTimer);

    m_helloTimer = new QTimer(this);
    m_helloTimer->setSingleShot(true);
    connect(m_helloTimer, &QTimer::timeout, this, &PluginHost::onHelloTimeout);
}

PluginHost::~PluginHost()
{
    stop();
}

void PluginHost::prepareHostEnvironment(QProcessEnvironment &env) const
{
    // Plugin host must run against MSYS2-prefixed Python (PyQt6 / pyzmq / Qt6
    // platform plugins + DLLs). When openbus is started from Explorer, PATH
    // often lacks ucrt64/bin — inject it from the chosen interpreter.
    const QFileInfo pyInfo(m_pythonExe);
    const QString pyBin = pyInfo.absolutePath();               // .../ucrt64/bin
    const QDir pyRoot = QFileInfo(pyBin).dir();               // .../ucrt64
    const QDir msysRoot = QFileInfo(pyRoot.absolutePath()).dir(); // .../msys64
    const QString usrBin = msysRoot.filePath(QStringLiteral("usr/bin"));

    QStringList pathParts;
    if (!pyBin.isEmpty())
        pathParts << QDir::toNativeSeparators(pyBin);
    if (QDir(usrBin).exists())
        pathParts << QDir::toNativeSeparators(usrBin);
    const QString existing = env.value(QStringLiteral("PATH"));
    if (!existing.isEmpty())
        pathParts << existing;
    env.insert(QStringLiteral("PATH"), pathParts.join(QLatin1Char(';')));

    // Mark the child as UCRT64 so native packages resolve consistently
    const QString prefixName = pyRoot.dirName().toLower(); // ucrt64 / mingw64 / ...
    if (prefixName == QLatin1String("ucrt64")
        || prefixName == QLatin1String("clang64")
        || prefixName == QLatin1String("mingw64")) {
        env.insert(QStringLiteral("MSYSTEM"), prefixName.toUpper());
        env.insert(QStringLiteral("MSYSTEM_PREFIX"),
                   QDir::toNativeSeparators(pyRoot.absolutePath()));
        env.insert(QStringLiteral("MINGW_PREFIX"),
                   QDir::toNativeSeparators(pyRoot.absolutePath()));
    }

    // openbus may inherit QT_PLUGIN_PATH=<exe>/plugins (Python plugin tree).
    // That breaks PyQt6 platforms — point at Qt shipped with MSYS2 Python.
    env.remove(QStringLiteral("QT_PLUGIN_PATH"));
    env.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
    const QString qtPlugins = pyRoot.filePath(QStringLiteral("share/qt6/plugins"));
    if (QDir(qtPlugins).exists()) {
        env.insert(QStringLiteral("QT_PLUGIN_PATH"),
                   QDir::toNativeSeparators(qtPlugins));
        env.insert(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"),
                   QDir::toNativeSeparators(qtPlugins + QStringLiteral("/platforms")));
    }
    // Never force offscreen for real plugin UI windows
    if (env.value(QStringLiteral("QT_QPA_PLATFORM"))
            .compare(QStringLiteral("offscreen"), Qt::CaseInsensitive) == 0)
        env.remove(QStringLiteral("QT_QPA_PLATFORM"));
}

bool PluginHost::start(const QString &pythonExe, const QString &hostScript,
                       const QString &sdkDir, const QString &pluginsDir)
{
    m_pythonExe = pythonExe;
    m_hostScript = hostScript;
    m_sdkDir = sdkDir;
    m_pluginsDir = pluginsDir;
    m_helloSeen = false;
    m_helloTimer->stop();

    if (!m_zmq) {
        m_zmq = new PluginZmqHub(this);
        connect(m_zmq, &PluginZmqHub::ctrlMessageReceived,
                this, &PluginHost::onCtrlMessage);
        connect(m_zmq, &PluginZmqHub::hubError, this, &PluginHost::hostError);
    }
    if (!m_zmq->isRunning()) {
        if (!m_zmq->start()) {
            emit hostError(QStringLiteral("Failed to start ZMQ hub"));
            return false;
        }
    }

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    // Avoid stdout pipe fill stalling the host before host.hello
    m_process->setStandardOutputFile(QProcess::nullDevice());

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    prepareHostEnvironment(env);
    if (!sdkDir.isEmpty())
        env.insert(QStringLiteral("SIN_SDK_DIR"), sdkDir);
    if (!pluginsDir.isEmpty())
        env.insert(QStringLiteral("SIN_PLUGINS_DIR"), pluginsDir);
    env.insert(QStringLiteral("SIN_ZMQ_CTRL"), m_zmq->ctrlEndpoint());
    env.insert(QStringLiteral("SIN_ZMQ_DATA"), m_zmq->dataEndpoint());
    const QString lang = AppConfig::instance()->getString(
        QStringLiteral("ui.language"), QStringLiteral("en"));
    if (!lang.isEmpty())
        env.insert(QStringLiteral("SIN_UI_LANGUAGE"), lang);
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process)
            return;
        const QByteArray err = m_process->readAllStandardError();
        if (!err.isEmpty())
            spdlog::info("PluginHost[stderr]: {}", err.trimmed().toStdString());
    });
    connect(m_process, &QProcess::finished,
            this, [this](int exitCode, QProcess::ExitStatus status) {
        onProcessFinished(exitCode, static_cast<int>(status));
    });
    connect(m_process, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
        onProcessError(static_cast<int>(error));
    });

    spdlog::info("PluginHost: starting Python host: {} {} (ctrl={} data={})",
                 pythonExe.toStdString(), hostScript.toStdString(),
                 m_zmq->ctrlEndpoint().toStdString(),
                 m_zmq->dataEndpoint().toStdString());

    m_process->start(pythonExe, QStringList() << hostScript);

    if (!m_process->waitForStarted(5000)) {
        spdlog::error("PluginHost: failed to start: {}",
                      m_process->errorString().toStdString());
        emit hostError(m_process->errorString());
        return false;
    }

    m_restartAttempts = 0;
    spdlog::info("PluginHost: process started (PID={}), waiting for host.hello",
                 m_process->processId());
    m_helloTimer->start(8000);
    return true;
}

void PluginHost::stop()
{
    m_restartAttempts = kStopped;
    m_restartTimer->stop();
    m_helloTimer->stop();
    m_helloSeen = false;

    if (m_process) {
        sendNotification(QStringLiteral("shutdown"));
        if (!m_process->waitForFinished(3000)) {
            spdlog::warn("PluginHost: host did not exit after shutdown, killing");
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }

    if (m_zmq) {
        m_zmq->stop();
    }
}

bool PluginHost::isRunning() const
{
    return isProcessAlive() && m_helloSeen;
}

bool PluginHost::isProcessAlive() const
{
    return m_process && m_process->state() == QProcess::Running;
}

bool PluginHost::ensureProcess()
{
    if (isProcessAlive())
        return true;
    if (m_pythonExe.isEmpty() || m_hostScript.isEmpty())
        return false;
    return start(m_pythonExe, m_hostScript, m_sdkDir, m_pluginsDir);
}

qint64 PluginHost::processId() const
{
    return isProcessAlive() ? m_process->processId() : 0;
}

void PluginHost::sendNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("method")] = method;
    msg[QStringLiteral("params")] = params;
    sendMessage(msg);
}

void PluginHost::sendRequest(const QString &method, const QJsonObject &params,
                             std::function<void(const QJsonValue &)> callback)
{
    int id = m_nextRequestId++;
    m_pendingRequests[id] = callback;

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("method")] = method;
    msg[QStringLiteral("params")] = params;
    msg[QStringLiteral("id")] = id;
    sendMessage(msg);
}

void PluginHost::sendRequest(const QString &method, const QJsonObject &params)
{
    sendRequest(method, params, nullptr);
}

void PluginHost::sendResponse(const QJsonValue &id, const QJsonValue &result)
{
    if (id.isNull() || id.isUndefined())
        return;

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("result")] = result;
    msg[QStringLiteral("id")] = id;
    sendMessage(msg);
}

void PluginHost::sendErrorResponse(const QJsonValue &id, int code, const QString &message)
{
    if (id.isNull() || id.isUndefined())
        return;

    QJsonObject err;
    err[QStringLiteral("code")] = code;
    err[QStringLiteral("message")] = message;

    QJsonObject msg;
    msg[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    msg[QStringLiteral("error")] = err;
    msg[QStringLiteral("id")] = id;
    sendMessage(msg);
}

void PluginHost::sendMessage(const QJsonObject &msg)
{
    if (!m_zmq || !m_zmq->isRunning()) {
        spdlog::warn("PluginHost: cannot send, ZMQ hub not running");
        return;
    }
    const QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    m_zmq->sendCtrl(data);
}

void PluginHost::onCtrlMessage(const QByteArray &jsonUtf8)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonUtf8, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        spdlog::warn("PluginHost: JSON parse error: {} raw={}",
                     parseError.errorString().toStdString(),
                     QString::fromUtf8(jsonUtf8.left(200)).toStdString());
        return;
    }
    handleJsonObject(doc.object());
}

void PluginHost::handleJsonObject(const QJsonObject &obj)
{
    const QString method = obj.value(QStringLiteral("method")).toString();
    const QJsonObject params = obj.value(QStringLiteral("params")).toObject();
    const QJsonValue id = obj.value(QStringLiteral("id"));

    if (id.isDouble() && (obj.contains(QStringLiteral("result"))
                          || obj.contains(QStringLiteral("error")))) {
        const int reqId = id.toInt();
        auto it = m_pendingRequests.find(reqId);
        if (it != m_pendingRequests.end()) {
            if (it.value()) {
                if (obj.contains(QStringLiteral("error"))) {
                    spdlog::warn("PluginHost: request {} error", reqId);
                    it.value()(QJsonValue());
                } else {
                    it.value()(obj.value(QStringLiteral("result")));
                }
            }
            m_pendingRequests.erase(it);
        }
        return;
    }

    if (method == QLatin1String("host.hello")) {
        if (!m_helloSeen) {
            m_helloSeen = true;
            m_helloTimer->stop();
            spdlog::info("PluginHost: host.hello received");
            emit hostStarted();
        }
        return;
    }

    emit messageReceived(method, params, id);
}

void PluginHost::onHelloTimeout()
{
    if (m_helloSeen)
        return;
    QString detail = QStringLiteral("Plugin host did not send host.hello within 8s");
    if (m_process) {
        const QByteArray err = m_process->readAllStandardError();
        if (!err.isEmpty())
            detail += QStringLiteral("\n") + QString::fromUtf8(err.left(500));
    }
    spdlog::error("PluginHost: {}", detail.toStdString());
    emit hostError(detail);
    // Kill so the next activatePlugin can restart a clean host
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

void PluginHost::onProcessFinished(int exitCode, int exitStatus)
{
    spdlog::info("PluginHost: Python host exited (code={}, status={})",
                 exitCode, exitStatus);
    m_helloSeen = false;

    if (m_restartAttempts < 0)
        return;

    if (exitCode != 0) {
        emit hostCrashed();
        scheduleRestart();
    }
}

void PluginHost::onProcessError(int error)
{
    Q_UNUSED(error);
    spdlog::error("PluginHost: process error: {}",
                  m_process ? m_process->errorString().toStdString() : "unknown");

    if (m_restartAttempts < 0)
        return;

    emit hostError(m_process ? m_process->errorString()
                             : QStringLiteral("unknown error"));
    scheduleRestart();
}

void PluginHost::onRestartTimer()
{
    if (m_restartAttempts == kStopped)
        return;

    spdlog::info("PluginHost: restart attempt {}", m_restartAttempts + 1);
    start(m_pythonExe, m_hostScript, m_sdkDir, m_pluginsDir);
}

void PluginHost::scheduleRestart()
{
    m_restartAttempts++;
    if (m_restartAttempts > 3) {
        spdlog::error("PluginHost: restart limit exceeded");
        emit hostError(QStringLiteral("Plugin host crashed repeatedly"));
        return;
    }
    const int delayMs = 1000 * (1 << (m_restartAttempts - 1));
    spdlog::info("PluginHost: restart in {} ms", delayMs);
    m_restartTimer->start(delayMs);
}
