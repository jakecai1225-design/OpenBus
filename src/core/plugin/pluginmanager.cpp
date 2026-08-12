#include "pluginmanager.h"
#include "pluginhost.h"
#include "core/canframe.h"
#include "core/logging.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcessEnvironment>

// ---- CanFrame ↔ JSON 转换 ----

static QJsonObject frameToJson(const CanFrame &f)
{
    QJsonObject obj;
    obj["id"] = static_cast<qint64>(f.id);
    obj["extended"] = f.extended;
    obj["fd"] = f.fd;
    obj["dlc"] = f.dlc;
    obj["data"] = QString::fromUtf8(f.data.toHex());
    obj["timestamp"] = f.timestamp;
    obj["channel"] = f.channel;
    obj["direction"] = (f.direction == CanFrame::Tx) ? "Tx" : "Rx";
    return obj;
}

static CanFrame jsonToFrame(const QJsonObject &obj)
{
    CanFrame f;
    f.id = static_cast<quint32>(obj.value("id").toVariant().toUInt());
    f.extended = obj.value("extended").toBool(false);
    f.fd = obj.value("fd").toBool(false);
    f.dlc = static_cast<quint8>(obj.value("dlc").toInt(0));
    f.data = QByteArray::fromHex(obj.value("data").toString().toUtf8());
    f.timestamp = obj.value("timestamp").toDouble(0.0);
    f.channel = static_cast<quint8>(obj.value("channel").toInt(1));
    f.direction = (obj.value("direction").toString() == "Tx")
                      ? CanFrame::Tx : CanFrame::Rx;
    return f;
}

// ---- PluginManager ----

PluginManager *PluginManager::instance()
{
    static PluginManager inst;
    return &inst;
}

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
}

QString PluginManager::findAppBaseDir() const
{
    // 按优先级查找：可执行文件目录 → 源码目录
    // 1. 可执行文件所在目录（发布版）
    QString exeDir = QCoreApplication::applicationDirPath();
    if (QDir(exeDir + "/plugins").exists() || QDir(exeDir + "/sdk").exists())
        return exeDir;

    // 2. 源码根目录（开发版）
    QString sourceRoot = QDir(exeDir).absoluteFilePath("../..");
    sourceRoot = QDir(sourceRoot).absolutePath();
    if (QDir(sourceRoot + "/plugins").exists() || QDir(sourceRoot + "/sdk").exists())
        return sourceRoot;

    // 3. 默认用 exeDir
    return exeDir;
}

QString PluginManager::findPythonExecutable() const
{
    // 按优先级查找 Python 解释器
    QStringList candidates;

    // 环境变量指定的 Python
    QString envPython = QProcessEnvironment::systemEnvironment().value("SIN_PYTHON");
    if (!envPython.isEmpty())
        candidates << envPython;

    // 系统路径中的 python / python3
    candidates << "python3"
               << "python"
               << "py";

    for (const auto &cmd : candidates) {
        QProcess proc;
        QStringList args;
        args << "-c" << "import sys; print(sys.executable)";
        proc.start(cmd, args);
        if (proc.waitForFinished(3000) && proc.exitCode() == 0) {
            QString path = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                spdlog::info("PluginManager: 找到 Python: {} ({})", path.toStdString(),
                             cmd.toStdString());
                return path;
            }
        }
    }

    spdlog::warn("PluginManager: 未找到系统 Python 解释器");
    return QString();
}

void PluginManager::discoverPlugins()
{
    m_plugins.clear();

    QString baseDir = findAppBaseDir();
    m_pluginsDir = baseDir + "/plugins";
    m_sdkDir = baseDir + "/sdk";
    m_hostScriptPath = baseDir + "/scripts/sin_host.py";

    QDir pluginsDir(m_pluginsDir);
    if (!pluginsDir.exists()) {
        spdlog::info("PluginManager: 插件目录不存在: {}", m_pluginsDir.toStdString());
        return;
    }

    QStringList entries = pluginsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        QString pluginDir = pluginsDir.absoluteFilePath(entry);
        PluginInfo info;
        if (info.loadFromDirectory(pluginDir)) {
            if (m_plugins.contains(info.name)) {
                spdlog::warn("PluginManager: 重复的插件名 '{}'，跳过 {}",
                             info.name.toStdString(), pluginDir.toStdString());
                continue;
            }
            m_plugins.insert(info.name, info);
        }
    }

    spdlog::info("PluginManager: 发现 {} 个插件", m_plugins.size());
}

void PluginManager::initialize()
{
    discoverPlugins();

    if (m_plugins.isEmpty()) {
        spdlog::info("PluginManager: 无插件，跳过宿主启动");
        return;
    }

    m_pythonExe = findPythonExecutable();
    if (m_pythonExe.isEmpty()) {
        spdlog::warn("PluginManager: 未找到 Python，插件系统不可用");
        return;
    }

    if (!QFileInfo::exists(m_hostScriptPath)) {
        spdlog::warn("PluginManager: 宿主脚本不存在: {}", m_hostScriptPath.toStdString());
        return;
    }

    m_host = new PluginHost(this);

    connect(m_host, &PluginHost::messageReceived,
            this, &PluginManager::handleHostMessage);
    connect(m_host, &PluginHost::hostCrashed, []() {
        spdlog::warn("PluginManager: 插件宿主崩溃");
    });

    if (!m_host->start(m_pythonExe, m_hostScriptPath, m_sdkDir, m_pluginsDir)) {
        spdlog::error("PluginManager: 插件宿主启动失败");
        return;
    }

    // 启动帧批量定时器（100ms）
    m_frameBatchTimerId = startTimer(100);

    // 激活 onStartup 插件
    onStartup();
}

void PluginManager::shutdown()
{
    if (m_frameBatchTimerId) {
        killTimer(m_frameBatchTimerId);
        m_frameBatchTimerId = 0;
    }

    if (m_host) {
        // 停用所有已激活插件
        for (const auto &name : m_activatedPlugins)
            m_host->sendNotification("deactivate", {{"plugin", name}});
        m_activatedPlugins.clear();

        m_host->stop();
        m_host->deleteLater();
        m_host = nullptr;
    }
}

QList<PluginInfo> PluginManager::discoveredPlugins() const
{
    return m_plugins.values();
}

bool PluginManager::isPluginEnabled(const QString &name) const
{
    return !m_disabledPlugins.contains(name);
}

void PluginManager::setPluginEnabled(const QString &name, bool enabled)
{
    if (enabled) {
        m_disabledPlugins.remove(name);
        activatePlugin(name);
    } else {
        deactivatePlugin(name);
        m_disabledPlugins.insert(name);
    }
    emit pluginListChanged();
}

void PluginManager::onStartup()
{
    if (!m_host || !m_host->isRunning())
        return;

    for (const auto &info : m_plugins) {
        if (m_disabledPlugins.contains(info.name))
            continue;
        if (info.activatesOnStartup() && !m_activatedPlugins.contains(info.name)) {
            activatePlugin(info.name);
        }
    }
}

void PluginManager::activatePlugin(const QString &name)
{
    if (!m_host || !m_host->isRunning())
        return;

    if (m_activatedPlugins.contains(name))
        return;

    if (!m_plugins.contains(name)) {
        spdlog::warn("PluginManager: 插件 '{}' 不存在", name.toStdString());
        return;
    }

    const PluginInfo &info = m_plugins[name];

    QJsonObject params;
    params["plugin"] = name;
    params["directory"] = info.directory;
    params["main"] = info.mainScript;

    m_host->sendNotification("activate", params);
    m_activatedPlugins.insert(name);

    spdlog::info("PluginManager: 激活插件 '{}'", name.toStdString());
}

void PluginManager::deactivatePlugin(const QString &name)
{
    if (!m_host || !m_host->isRunning())
        return;

    if (!m_activatedPlugins.contains(name))
        return;

    m_host->sendNotification("deactivate", {{"plugin", name}});
    m_activatedPlugins.remove(name);

    spdlog::info("PluginManager: 停用插件 '{}'", name.toStdString());
}

void PluginManager::onFrameReceived(const CanFrame &frame)
{
    // 只在有 onFrame 插件时缓冲
    bool hasFramePlugin = false;
    for (const auto &name : m_activatedPlugins) {
        if (m_plugins[name].activatesOnFrame()) {
            hasFramePlugin = true;
            break;
        }
    }
    if (!hasFramePlugin)
        return;

    m_frameBuffer.append(frame);
}

void PluginManager::onCommandExecuted(const QString &commandId)
{
    if (!m_host || !m_host->isRunning())
        return;

    // 检查是否有插件监听此命令的激活事件
    for (const auto &info : m_plugins) {
        if (m_disabledPlugins.contains(info.name))
            continue;
        if (!m_activatedPlugins.contains(info.name) &&
            info.hasActivationEvent("onCommand:" + commandId)) {
            activatePlugin(info.name);
        }
    }

    m_host->sendNotification("executeCommand", {{"id", commandId}});
}

void PluginManager::executeCommand(const QString &commandId)
{
    onCommandExecuted(commandId);
}

void PluginManager::onFileOpened(const QString &path, const QString &extension)
{
    if (!m_host || !m_host->isRunning())
        return;

    // 检查是否有插件监听此文件扩展名的激活事件
    QString event = "onFileOpen:" + extension;
    for (const auto &info : m_plugins) {
        if (m_disabledPlugins.contains(info.name))
            continue;
        if (!m_activatedPlugins.contains(info.name) &&
            info.hasActivationEvent(event)) {
            activatePlugin(info.name);
        }
    }

    m_host->sendNotification("fileOpened", {
        {"path", path},
        {"extension", extension}
    });
}

void PluginManager::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_frameBatchTimerId && m_host && m_host->isRunning()) {
        if (!m_frameBuffer.isEmpty()) {
            QJsonArray framesArray;
            for (const auto &f : m_frameBuffer)
                framesArray.append(frameToJson(f));
            m_frameBuffer.clear();

            m_host->sendNotification("frameReceived", {{"frames", framesArray}});
        }
    }
}

void PluginManager::handleHostMessage(const QString &method,
                                       const QJsonObject &params,
                                       const QJsonValue &id)
{
    if (method == "output.append") {
        emit outputMessage(params.value("text").toString());
    }
    else if (method == "output.clear") {
        emit outputClearRequested();
    }
    else if (method == "sendFrame") {
        CanFrame frame = jsonToFrame(params);
        frame.direction = CanFrame::Tx;
        emit sendFrameRequested(frame);
    }
    else if (method == "registerCommand") {
        QString cmdId = params.value("id").toString();
        QString title = params.value("title").toString();
        emit commandRegistered(cmdId, title);
    }
    else if (method == "log") {
        int level = params.value("level").toInt();
        QString message = params.value("message").toString();
        emit pluginLog(level, message);
        spdlog::info("Plugin: [{}] {}", level == 0 ? "INFO" : "ERROR",
                     message.toStdString());
    }
    else if (method == "frames.getSelected") {
        // 请求 — 通过信号请求 MainWindow 提供选中帧
        emit requestSelectedFrames(id);
    }
    else if (method == "frames.getRecent") {
        int count = params.value("count").toInt(100);
        emit requestRecentFrames(id, count);
    }
    else {
        spdlog::warn("PluginManager: 未知方法 '{}'", method.toStdString());
    }
}

void PluginManager::provideSelectedFrames(const QJsonValue &requestId, const QList<CanFrame> &frames)
{
    if (!m_host)
        return;
    QJsonArray arr;
    for (const auto &f : frames)
        arr.append(frameToJson(f));
    m_host->sendResponse(requestId, QJsonObject{{"frames", arr}});
}

void PluginManager::provideRecentFrames(const QJsonValue &requestId, const QList<CanFrame> &frames)
{
    if (!m_host)
        return;
    QJsonArray arr;
    for (const auto &f : frames)
        arr.append(frameToJson(f));
    m_host->sendResponse(requestId, QJsonObject{{"frames", arr}});
}
