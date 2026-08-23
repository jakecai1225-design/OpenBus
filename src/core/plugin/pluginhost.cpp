#include "pluginhost.h"
#include "core/logging.h"

#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTimer>
#include <QDir>

PluginHost::PluginHost(QObject *parent)
    : QObject(parent)
{
    m_restartTimer = new QTimer(this);
    m_restartTimer->setSingleShot(true);
    connect(m_restartTimer, &QTimer::timeout, this, &PluginHost::onRestartTimer);
}

PluginHost::~PluginHost()
{
    stop();
}

bool PluginHost::start(const QString &pythonExe, const QString &hostScript,
                       const QString &sdkDir, const QString &pluginsDir)
{
    m_pythonExe = pythonExe;
    m_hostScript = hostScript;
    m_sdkDir = sdkDir;
    m_pluginsDir = pluginsDir;

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    // 设置环境变量供 sin_host.py 读取
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!sdkDir.isEmpty())
        env.insert("SIN_SDK_DIR", sdkDir);
    if (!pluginsDir.isEmpty())
        env.insert("SIN_PLUGINS_DIR", pluginsDir);
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PluginHost::onReadyRead);
    connect(m_process, &QProcess::finished,
            this, [this](int exitCode, QProcess::ExitStatus status) {
        onProcessFinished(exitCode, static_cast<int>(status));
    });
    connect(m_process, &QProcess::errorOccurred,
            this, [this](QProcess::ProcessError error) {
        onProcessError(static_cast<int>(error));
    });

    QStringList args;
    args << hostScript;

    spdlog::info("PluginHost: 启动 Python 宿主: {} {}", pythonExe.toStdString(),
                 hostScript.toStdString());

    m_process->start(pythonExe, args);

    if (!m_process->waitForStarted(5000)) {
        spdlog::error("PluginHost: Python 宿主启动失败: {}",
                      m_process->errorString().toStdString());
        emit hostError(m_process->errorString());
        return false;
    }

    m_restartAttempts = 0;
    spdlog::info("PluginHost: Python 宿主已启动 (PID={})",
                 m_process->processId());
    emit hostStarted();
    return true;
}

void PluginHost::stop()
{
    // 停机标志优先：后续 kill 触发的 onProcessFinished/onProcessError
    // 据此跳过自动重启（否则应用退出后 1s 会拉起僵尸宿主进程）
    m_restartAttempts = kStopped;
    m_restartTimer->stop();

    if (!m_process)
        return;

    // 发送 shutdown 通知让 Python 优雅退出
    sendNotification("shutdown");

    if (!m_process->waitForFinished(3000)) {
        spdlog::warn("PluginHost: Python 宿主未响应 shutdown，强制终止");
        m_process->kill();
        m_process->waitForFinished(1000);
    }

    m_process->deleteLater();
    m_process = nullptr;
}

bool PluginHost::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

qint64 PluginHost::processId() const
{
    return (m_process && m_process->state() == QProcess::Running)
               ? m_process->processId() : 0;
}

void PluginHost::sendNotification(const QString &method, const QJsonObject &params)
{
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["method"] = method;
    msg["params"] = params;
    sendMessage(msg);
}

void PluginHost::sendRequest(const QString &method, const QJsonObject &params,
                             std::function<void(const QJsonValue &)> callback)
{
    int id = m_nextRequestId++;
    m_pendingRequests[id] = callback;

    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["method"] = method;
    msg["params"] = params;
    msg["id"] = id;
    sendMessage(msg);
}

void PluginHost::sendRequest(const QString &method, const QJsonObject &params)
{
    sendRequest(method, params, nullptr);
}

void PluginHost::sendResponse(const QJsonValue &id, const QJsonValue &result)
{
    if (!isRunning() || id.isNull() || id.isUndefined())
        return;

    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["result"] = result;
    msg["id"] = id;
    sendMessage(msg);
}

void PluginHost::sendErrorResponse(const QJsonValue &id, int code, const QString &message)
{
    if (!isRunning() || id.isNull() || id.isUndefined())
        return;

    QJsonObject err;
    err["code"] = code;
    err["message"] = message;

    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["error"] = err;
    msg["id"] = id;
    sendMessage(msg);
}

void PluginHost::sendMessage(const QJsonObject &msg)
{
    if (!isRunning()) {
        spdlog::warn("PluginHost: 无法发送消息，宿主未运行");
        return;
    }

    QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    data.append('\n');
    m_process->write(data);
}

void PluginHost::onReadyRead()
{
    if (!m_process)
        return;

    m_readBuffer.append(m_process->readAllStandardOutput());

    // 按行处理（newline-delimited JSON）
    int idx;
    while ((idx = m_readBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_readBuffer.left(idx).trimmed();
        m_readBuffer.remove(0, idx + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

void PluginHost::handleLine(const QByteArray &line)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        spdlog::warn("PluginHost: JSON 解析错误: {} (原始: {})",
                     parseError.errorString().toStdString(),
                     QString::fromUtf8(line).toStdString());
        return;
    }

    QJsonObject obj = doc.object();
    QString method = obj.value("method").toString();
    QJsonObject params = obj.value("params").toObject();
    QJsonValue id = obj.value("id");

    // 如果是响应（有 result 或 error，且有 id）
    if (id.isDouble() && (obj.contains("result") || obj.contains("error"))) {
        int reqId = id.toInt();
        auto it = m_pendingRequests.find(reqId);
        if (it != m_pendingRequests.end()) {
            if (it.value()) {
                if (obj.contains("error")) {
                    spdlog::warn("PluginHost: 请求 {} 返回错误: {}",
                                 reqId,
                                 QString::fromUtf8(QJsonDocument(obj.value("error").toObject()).toJson(QJsonDocument::Compact)).toStdString());
                    it.value()(QJsonValue());
                } else {
                    it.value()(obj.value("result"));
                }
            }
            m_pendingRequests.erase(it);
        }
        return;
    }

    // 否则是通知/请求
    emit messageReceived(method, params, id);
}

void PluginHost::onProcessFinished(int exitCode, int exitStatus)
{
    spdlog::info("PluginHost: Python 宿主退出 (code={}, status={})",
                 exitCode, exitStatus);

    if (m_restartAttempts < 0)
        return;  // stop() 主动终止

    // 非正常退出 → 尝试重启
    if (exitCode != 0) {
        emit hostCrashed();
        scheduleRestart();
    }
}

void PluginHost::onProcessError(int error)
{
    spdlog::error("PluginHost: 进程错误: {}",
                  m_process ? m_process->errorString().toStdString() : "unknown");

    if (m_restartAttempts < 0)
        return;  // stop() 主动终止

    emit hostError(m_process ? m_process->errorString() : QStringLiteral("未知错误"));
    scheduleRestart();
}

void PluginHost::onRestartTimer()
{
    if (m_restartAttempts == kStopped)
        return;   // stop() 竞态保护

    spdlog::info("PluginHost: 尝试重启 (第 {} 次)", m_restartAttempts + 1);
    start(m_pythonExe, m_hostScript, m_sdkDir, m_pluginsDir);
}

void PluginHost::scheduleRestart()
{
    m_restartAttempts++;

    if (m_restartAttempts > 3) {
        spdlog::error("PluginHost: 重启超过 3 次，放弃");
        emit hostError(QStringLiteral("插件宿主多次崩溃，已停止自动重启"));
        return;
    }

    // 指数退避: 1s → 2s → 4s
    int delayMs = 1000 * (1 << (m_restartAttempts - 1));
    spdlog::info("PluginHost: {}ms 后重启", delayMs);
    m_restartTimer->start(delayMs);
}
