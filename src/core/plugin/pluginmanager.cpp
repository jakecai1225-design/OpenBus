#include "pluginmanager.h"
#include "pluginhost.h"
#include "pluginzmq.h"
#include "pluginconvertjob.h"
#include "core/canframe.h"
#include "core/logging.h"
#include "core/dbcdata.h"
#include "core/dbcmanager.h"
#include "core/projectmanager.h"
#include "core/appconfig.h"
#include "core/dbc/dbc_adapter.h"
#include "core/dbc/dbc_writer.h"

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

// 数据链路批处理参数（方案 §二：每 100ms 批量，每批 ≤100 帧；
// 缓冲上限防慢消费者（宿主单线程，插件回调阻塞时）无界增长）
static constexpr int kFramesPerBatch = 100;
static constexpr int kMaxBufferedFrames = 50000;


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
    // 有意泄漏不析构（DEF-07）：静态对象析构在 DLL 宿主场景发生于
    // DLL_PROCESS_DETACH（loader lock）——~PluginHost → stop() → QProcess
    // 写管道 → QWindowsPipeWriter 创建线程池等待对象，在加载器锁下崩溃。
    // Python 宿主改经 qApp aboutToQuit 优雅关闭（见 startHostIfNeeded）。
    static PluginManager *inst = new PluginManager();
    return inst;
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

QString PluginManager::resolvePluginsDir(const QString &baseDir) const
{
    // Explicit override always wins (dev scripts / CI).
    const QString fromEnv = qEnvironmentVariable("SIN_PLUGINS_DIR");
    if (!fromEnv.isEmpty() && QDir(fromEnv).exists())
        return QDir(fromEnv).absolutePath();

    // Force market/.opk extract under build/bin/plugins (ignore source tree).
    const QString useInstalled = qEnvironmentVariable("SIN_USE_INSTALLED_PLUGINS");
    if (useInstalled == QLatin1String("1")
        || useInstalled.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return baseDir + QStringLiteral("/plugins");
    }

    // Dev: openbus.exe lives in build/bin → ../../plugins is the live source tree.
    // Python suite edits under plugins/<id> then take effect on next activate
    // without re-packing .opk into build/bin/plugins.
    const QString exeDir = QCoreApplication::applicationDirPath();
    QString sourceRoot = QDir(exeDir).absoluteFilePath(QStringLiteral("../.."));
    sourceRoot = QDir(sourceRoot).canonicalPath();
    if (sourceRoot.isEmpty())
        sourceRoot = QDir(exeDir).absoluteFilePath(QStringLiteral("../.."));
    const QString sourcePlugins =
        QDir(sourceRoot).filePath(QStringLiteral("plugins"));
    if (QDir(sourcePlugins).exists()
        && QFileInfo(QDir(sourcePlugins).filePath(QStringLiteral("_shared")))
               .isDir()) {
        return QDir(sourcePlugins).absolutePath();
    }

    return baseDir + QStringLiteral("/plugins");
}

QString PluginManager::installPluginsDir() const
{
    // .opk install must not overwrite the git source tree.
    return QCoreApplication::applicationDirPath() + QStringLiteral("/plugins");
}

void PluginManager::scanPluginsDirectory(const QString &pluginsDir, bool skipExisting)
{
    QDir dir(pluginsDir);
    if (!dir.exists())
        return;

    const QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &entry : entries) {
        if (entry == QLatin1String("_shared"))
            continue;
        const QString pluginDir = dir.absoluteFilePath(entry);
        PluginInfo info;
        if (!info.loadFromDirectory(pluginDir))
            continue;
        if (m_plugins.contains(info.name)) {
            if (skipExisting) {
                spdlog::debug("PluginManager: skip installed duplicate '{}'",
                              info.name.toStdString());
                continue;
            }
            spdlog::warn("PluginManager: 重复的插件名 '{}'，跳过 {}",
                         info.name.toStdString(), pluginDir.toStdString());
            continue;
        }
        m_plugins.insert(info.name, info);
    }
}

QString PluginManager::findPythonExecutable() const
{
    return resolvePluginPython();
}

QString PluginManager::resolvePluginPython()
{
    auto looksLikeMsysPrefixBin = [](const QString &exePath) -> bool {
        const QString n = QDir::fromNativeSeparators(exePath).toLower();
        // .../ucrt64/bin/python.exe  or clang64 / mingw64
        return n.contains(QLatin1String("/ucrt64/bin/"))
            || n.contains(QLatin1String("/clang64/bin/"))
            || n.contains(QLatin1String("/mingw64/bin/"));
    };

    auto probePython = [](const QString &cmd) -> QString {
        if (cmd.isEmpty())
            return {};
        QProcess proc;
        // Require the same stack the plugin host needs (MSYS2 packages).
        proc.start(cmd, {
            QStringLiteral("-c"),
            QStringLiteral(
                "import sys\n"
                "import PyQt6\n"
                "import zmq\n"
                "print(sys.executable)")
        });
        if (!proc.waitForFinished(8000) || proc.exitCode() != 0)
            return {};
        const QString path =
            QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (path.isEmpty() || !QFileInfo::exists(path))
            return {};
        return QDir::toNativeSeparators(path);
    };

    QStringList candidates;

    const QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    const QString envPython = sysEnv.value(QStringLiteral("SIN_PYTHON"));
    if (!envPython.isEmpty())
        candidates << envPython;

    // Active MSYS2 prefix (when openbus was launched from an UCRT64 shell)
    const QStringList prefixEnvKeys = {
        QStringLiteral("MSYSTEM_PREFIX"),
        QStringLiteral("MINGW_PREFIX"),
    };
    for (const QString &key : prefixEnvKeys) {
        const QString prefix = sysEnv.value(key);
        if (prefix.isEmpty())
            continue;
        const QString py = QDir(prefix).filePath(QStringLiteral("bin/python.exe"));
        if (QFileInfo::exists(py))
            candidates << QDir::toNativeSeparators(py);
    }

    // Common MSYS2 install roots → prefer ucrt64 (matches openbus toolchain)
    QStringList roots;
    const QString sinMsys = sysEnv.value(QStringLiteral("SIN_MSYS2"));
    if (!sinMsys.isEmpty())
        roots << sinMsys;
    const QString msysPrefix = sysEnv.value(QStringLiteral("MSYS2_PREFIX"));
    if (!msysPrefix.isEmpty())
        roots << msysPrefix;
    roots << QStringLiteral("C:/msys64")
          << QStringLiteral("D:/msys64")
          << QStringLiteral("C:/msys2")
          << QStringLiteral("D:/msys2");

    const QStringList envs = {
        QStringLiteral("ucrt64"),
        QStringLiteral("clang64"),
        QStringLiteral("mingw64"),
    };
    for (const QString &root : roots) {
        for (const QString &envName : envs) {
            const QString py = QDir(root).filePath(envName + QStringLiteral("/bin/python.exe"));
            if (QFileInfo::exists(py))
                candidates << QDir::toNativeSeparators(py);
        }
    }

    // Optional portable bundle (must still provide PyQt6 + zmq)
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("runtime/python/python.exe"));
    if (QFileInfo::exists(bundled))
        candidates << QDir::toNativeSeparators(bundled);

    // Deduplicate while preserving order
    QStringList unique;
    for (const QString &c : candidates) {
        if (!unique.contains(c, Qt::CaseInsensitive))
            unique << c;
    }

    for (const QString &cmd : unique) {
        const QString path = probePython(cmd);
        if (path.isEmpty()) {
            spdlog::debug("PluginManager: Python candidate rejected (need PyQt6+zmq): {}",
                          cmd.toStdString());
            continue;
        }
        // SIN_PYTHON / bundled may live outside msys — accept if probe passed.
        // Auto-discovered PATH names are not in the list; only explicit paths.
        spdlog::info("PluginManager: using plugin Python: {} (via {})",
                     path.toStdString(), cmd.toStdString());
        return path;
    }

    // Last resort: only accept `python` on PATH if it is clearly MSYS2 *64
    // and imports PyQt6+zmq (blocks Windows Store / official python.org).
    for (const QString &cmd : {QStringLiteral("python"), QStringLiteral("python3")}) {
        const QString path = probePython(cmd);
        if (path.isEmpty())
            continue;
        if (!looksLikeMsysPrefixBin(path)) {
            spdlog::warn("PluginManager: ignoring non-MSYS2 Python on PATH: {}",
                         path.toStdString());
            continue;
        }
        spdlog::info("PluginManager: using plugin Python from PATH: {}",
                     path.toStdString());
        return path;
    }

    spdlog::warn("PluginManager: no MSYS2 Python with PyQt6+pyzmq found "
                 "(install mingw-w64-ucrt-x86_64-python-pyqt6 and "
                 "mingw-w64-ucrt-x86_64-python-pyzmq, or set SIN_PYTHON)");
    return {};
}

void PluginManager::discoverPlugins()
{
    m_plugins.clear();

    QString baseDir = findAppBaseDir();
    // sdk + host scripts stay beside the executable (POST_BUILD copies).
    m_sdkDir = baseDir + "/sdk";
    m_hostScriptPath = baseDir + "/scripts/sin_host.py";
    // Python suites: prefer live source plugins/ when developing from build/bin.
    m_pluginsDir = resolvePluginsDir(baseDir);

    if (!QDir(m_pluginsDir).exists()) {
        spdlog::info("PluginManager: 插件目录不存在: {}", m_pluginsDir.toStdString());
        return;
    }

    scanPluginsDirectory(m_pluginsDir, false);

    // Also pick up .opk-only installs under build/bin/plugins (source wins on name clash).
    const QString installed = installPluginsDir();
    if (QDir::cleanPath(installed) != QDir::cleanPath(m_pluginsDir)
        && QDir(installed).exists()) {
        scanPluginsDirectory(installed, true);
    }

    spdlog::info("PluginManager: 发现 {} 个插件 (dir={})",
                 m_plugins.size(), m_pluginsDir.toStdString());

    // 重算懒激活候选集，并清理订阅表中已卸载的插件
    m_onFramePlugins.clear();
    for (const auto &info : m_plugins)
        if (info.activatesOnFrame())
            m_onFramePlugins.insert(info.name);
    m_frameSubscribers.intersect(QSet<QString>(m_plugins.keyBegin(), m_plugins.keyEnd()));
}

void PluginManager::initialize()
{
    discoverPlugins();
    startHostIfNeeded();

    // 插件不自动激活，由用户在插件市场面板中启动
}

void PluginManager::startHostIfNeeded()
{
    if (m_host && m_host->isProcessAlive())
        return;   // host process already up (hello may still be pending)

    if (m_plugins.isEmpty()) {
        spdlog::info("PluginManager: no plugins, skip host start");
        return;
    }

    if (m_pythonExe.isEmpty())
        m_pythonExe = findPythonExecutable();
    if (m_pythonExe.isEmpty()) {
        spdlog::warn("PluginManager: MSYS2 Python (PyQt6+pyzmq) not found, "
                     "plugin system unavailable");
        return;
    }

    if (!QFileInfo::exists(m_hostScriptPath)) {
        spdlog::warn("PluginManager: host script missing: {}",
                     m_hostScriptPath.toStdString());
        return;
    }

    if (m_host) {
        // Process died but PluginHost object remains — restart with cached args
        if (!m_host->ensureProcess())
            spdlog::error("PluginManager: failed to restart plugin host");
        return;
    }

    m_host = new PluginHost(this);

    if (qApp)
        connect(qApp, &QCoreApplication::aboutToQuit,
                this, &PluginManager::shutdown);

    connect(m_host, &PluginHost::messageReceived,
            this, &PluginManager::handleHostMessage);
    connect(m_host, &PluginHost::hostStarted,
            this, &PluginManager::onHostStarted);
    connect(m_host, &PluginHost::hostCrashed, []() {
        spdlog::warn("PluginManager: plugin host crashed");
    });
    connect(m_host, &PluginHost::hostError, this, [this](const QString &err) {
        spdlog::error("PluginManager: host error: {}", err.toStdString());
        emit outputMessage(QStringLiteral("[plugin host] ") + err);
    });

    if (!m_host->start(m_pythonExe, m_hostScriptPath, m_sdkDir, m_pluginsDir)) {
        spdlog::error("PluginManager: failed to start plugin host");
        return;
    }

    if (!m_frameBatchTimerId)
        m_frameBatchTimerId = startTimer(100);
}

void PluginManager::onHostStarted()
{
    if (!m_frameSubscribers.isEmpty())
        spdlog::info("PluginManager: host restarted, rebuilding frame subscriptions");
    m_frameSubscribers.clear();

    // Crash recovery: re-activate plugins that were marked active
    if (!m_activatedPlugins.isEmpty()) {
        spdlog::info("PluginManager: host restarted, re-activating {} plugin(s)",
                     m_activatedPlugins.size());
        const QStringList names = m_activatedPlugins.values();
        m_activatedPlugins.clear();
        for (const auto &name : names) {
            if (!m_plugins.contains(name))
                continue;
            activatePlugin(name);
        }
    }

    // First-start / race: flush activations requested before host.hello
    if (!m_pendingActivations.isEmpty()) {
        const QStringList pending = m_pendingActivations;
        m_pendingActivations.clear();
        spdlog::info("PluginManager: flushing {} pending activation(s)",
                     pending.size());
        for (const auto &name : pending)
            activatePlugin(name);
    }
}

void PluginManager::shutdown()
{
    m_pendingActivations.clear();

    if (m_frameBatchTimerId) {
        killTimer(m_frameBatchTimerId);
        m_frameBatchTimerId = 0;
    }

    if (m_host) {
        // 停用所有已激活插件
        for (const auto &name : m_activatedPlugins)
            m_host->sendNotification("deactivate", {{"plugin", name}});
        m_activatedPlugins.clear();
        m_frameSubscribers.clear();

        m_host->stop();
        m_host->deleteLater();
        m_host = nullptr;
    }

    // 清理 DBC 会话与转换 Job（G9）
    qDeleteAll(m_dbcSessions);
    m_dbcSessions.clear();
    for (auto *job : m_convertJobs)
        job->requestCancel();
}

QList<PluginInfo> PluginManager::discoveredPlugins() const
{
    return m_plugins.values();
}

bool PluginManager::isPluginEnabled(const QString &name) const
{
    return !m_disabledPlugins.contains(name);
}

bool PluginManager::isPluginActivated(const QString &name) const
{
    return m_activatedPlugins.contains(name);
}

void PluginManager::setPluginEnabled(const QString &name, bool enabled)
{
    if (enabled) {
        // 仅标记为可用，不自动激活
        m_disabledPlugins.remove(name);
    } else {
        // 禁用时停用插件
        deactivatePlugin(name);
        m_disabledPlugins.insert(name);
    }
    emit pluginListChanged();
}

void PluginManager::reactivatePlugin(const QString &name)
{
    if (m_disabledPlugins.contains(name))
        m_disabledPlugins.remove(name);

    // Ensure host is up before deactivate/activate (install-then-run race)
    startHostIfNeeded();

    if (m_activatedPlugins.contains(name))
        deactivatePlugin(name);
    activatePlugin(name);

    emit pluginListChanged();
}

void PluginManager::activatePlugin(const QString &name)
{
    if (m_disabledPlugins.contains(name)) {
        spdlog::warn("PluginManager: refuse activate disabled plugin '{}'",
                     name.toStdString());
        return;
    }

    if (!m_plugins.contains(name)) {
        spdlog::warn("PluginManager: plugin '{}' not found", name.toStdString());
        return;
    }

    startHostIfNeeded();
    if (!m_host) {
        spdlog::warn("PluginManager: cannot activate '{}': host unavailable",
                     name.toStdString());
        return;
    }

    // Wait for host.hello before sending activate (ROUTER drops early messages)
    if (!m_host->isRunning()) {
        if (!m_pendingActivations.contains(name))
            m_pendingActivations.append(name);
        spdlog::info("PluginManager: queued activate '{}' (waiting for host.hello)",
                     name.toStdString());
        return;
    }

    if (m_activatedPlugins.contains(name))
        return;

    // Domain plugins share top-level Python names (app_shell, pages, …).
    // Keep only one loaded or the next open reuses the previous AppShell UI.
    if (usesSharedSuiteModules(name)) {
        const QStringList active = m_activatedPlugins.values();
        for (const QString &other : active) {
            if (other != name && usesSharedSuiteModules(other))
                deactivatePlugin(other);
        }
    }

    const PluginInfo &info = m_plugins[name];

    QJsonObject params;
    params[QStringLiteral("plugin")] = name;
    params[QStringLiteral("directory")] = info.directory;
    params[QStringLiteral("main")] = info.mainScript;

    m_host->sendNotification(QStringLiteral("activate"), params);
    m_activatedPlugins.insert(name);

    spdlog::info("PluginManager: activated plugin '{}'", name.toStdString());

    // Open primary UI command if declared (onCommand:xxx)
    for (const QString &ev : info.activationEvents) {
        if (ev.startsWith(QStringLiteral("onCommand:"))) {
            const QString cmdId = ev.mid(QStringLiteral("onCommand:").size());
            if (!cmdId.isEmpty())
                m_host->sendNotification(QStringLiteral("executeCommand"),
                                         {{QStringLiteral("id"), cmdId}});
            break;
        }
    }
}

bool PluginManager::isHostRunning() const
{
    return m_host && m_host->isRunning();
}

qint64 PluginManager::hostProcessId() const
{
    return m_host ? m_host->processId() : 0;
}

void PluginManager::onStartup()
{
    startHostIfNeeded();
    if (!m_host)
        return;

    for (const auto &info : m_plugins) {
        if (m_disabledPlugins.contains(info.name))
            continue;
        if (info.activatesOnStartup() && !m_activatedPlugins.contains(info.name))
            activatePlugin(info.name);
    }
}

void PluginManager::deactivatePlugin(const QString &name)
{
    if (!m_host || !m_host->isRunning())
        return;

    if (!m_activatedPlugins.contains(name))
        return;

    m_host->sendNotification("deactivate", {{"plugin", name}});
    m_activatedPlugins.remove(name);
    m_frameSubscribers.remove(name);

    spdlog::info("PluginManager: 停用插件 '{}'", name.toStdString());
}

bool PluginManager::usesSharedSuiteModules(const QString &name) const
{
    if (!m_plugins.contains(name))
        return false;
    const QString path = m_plugins[name].directory
                         + QStringLiteral("/app_shell.py");
    return QFileInfo::exists(path);
}

void PluginManager::onFrameReceived(const CanFrame &frame)
{
    // 懒激活（方案 §二 activationEvents）：声明 onFrame 的未激活插件
    // 在首帧到达时激活（集合通常为空 → 零开销）
    for (const auto &name : m_onFramePlugins) {
        if (!m_activatedPlugins.contains(name) && !m_disabledPlugins.contains(name))
            activatePlugin(name);
    }

    // 订阅制门控（方案 §一 5.1「无订阅 = 零开销」）：仅当存在
    // 已通过 context.on_frame() 注册回调（subscribeFrames 登记）的
    // 已激活插件时才缓冲。插件清单是否声明 onFrame 激活事件不作为
    // 数据推送依据（那是懒激活触发器，不是数据订阅）。
    if (m_frameSubscribers.isEmpty())
        return;

    m_frameBuffer.append(frame);

    // 慢消费者保护：宿主单线程，插件回调阻塞时丢最旧帧保内存有界
    if (m_frameBuffer.size() > kMaxBufferedFrames) {
        m_frameBuffer.remove(0, m_frameBuffer.size() - kMaxBufferedFrames);
        ++m_droppedFrames;
    }
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

void PluginManager::notifyLanguageChanged(const QString &locale)
{
    if (!m_host || !m_host->isRunning())
        return;
    m_host->sendNotification(QStringLiteral("setLanguage"),
                             QJsonObject{{QStringLiteral("locale"), locale}});
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
        if (m_droppedFrames > 0) {
            spdlog::warn("PluginManager: 帧缓冲达到上限，丢弃最旧 {} 帧",
                         m_droppedFrames);
            m_droppedFrames = 0;
        }
        if (!m_frameBuffer.isEmpty()) {
            // Scheme B: binary FRAME_BATCH over ZMQ PUB (chunks of <=100).
            PluginZmqHub *hub = m_host->zmqHub();
            const int total = m_frameBuffer.size();
            for (int off = 0; off < total; off += kFramesPerBatch) {
                const int end = qMin(total, off + kFramesPerBatch);
                QList<CanFrame> chunk;
                chunk.reserve(end - off);
                for (int i = off; i < end; ++i)
                    chunk.append(m_frameBuffer.at(i));
                if (hub)
                    hub->publishFrameBatch(chunk);
            }
            m_frameBuffer.clear();
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
        // dlc 补齐：SDK frames.send 不传 dlc（jsonToFrame 默认 0），
        // 按数据长度推导（PEAK FD 等驱动按 dlc 计算发送长度，缺失会发空帧）
        frame.dlc = CanFrame::lengthToDlc(frame.data.size());
        emit sendFrameRequested(frame);
    }
    else if (method == "registerCommand") {
        QString cmdId = params.value("id").toString();
        QString title = params.value("title").toString();
        emit commandRegistered(cmdId, title);
    }
    else if (method == "pluginWindowClosed") {
        // 插件窗口关闭 → 停用插件
        QString pluginName = params.value("plugin").toString();
        if (!pluginName.isEmpty()) {
            spdlog::info("PluginManager: 插件 '{}' 窗口关闭，自动停用", pluginName.toStdString());
            deactivatePlugin(pluginName);
            emit pluginListChanged();
        }
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
    // ---- files.* / dbc.*（G9 工具插件 API）----
    else if (method == "files.convertStart") {
        handleFilesConvertStart(params, id);
    }
    else if (method == "files.convertCancel") {
        handleFilesConvertCancel(params, id);
    }
    else if (method == "dbc.open") {
        handleDbcOpen(params, id);
    }
    else if (method == "dbc.messages") {
        handleDbcMessages(params, id);
    }
    else if (method == "dbc.signals") {
        handleDbcSignals(params, id);
    }
    else if (method == "dbc.updateSignal") {
        handleDbcUpdateSignal(params, id);
    }
    else if (method == "dbc.updateMessage") {
        handleDbcUpdateMessage(params, id);
    }
    else if (method == "dbc.save") {
        handleDbcSave(params, id);
    }
    else if (method == "dbc.close") {
        handleDbcClose(params, id);
    }
    // ---- 数据链路订阅（方案 §一 5.1 订阅制的 v1 落地）----
    else if (method == "subscribeFrames") {
        // 插件 context.on_frame() 注册首个回调时上报；仅接受已激活插件
        const QString name = params.value("plugin").toString();
        if (m_activatedPlugins.contains(name)) {
            m_frameSubscribers.insert(name);
            spdlog::info("PluginManager: 插件 '{}' 订阅帧数据", name.toStdString());
        }
    }
    // ---- signals.* / workspace.*（方案 §4.4 控制链路方法补齐）----
    else if (method == "signals.decode") {
        handleSignalsDecode(params, id);
    }
    else if (method == "signals.encode") {
        handleSignalsEncode(params, id);
    }
    else if (method == "workspace.getProjectDir") {
        handleWorkspaceProjectDir(params, id);
    }
    else if (method == "workspace.getDbcFiles") {
        handleWorkspaceDbcFiles(params, id);
    }
    else if (method == "workspace.getSetting") {
        handleWorkspaceGetSetting(params, id);
    }
    else if (method == "executeCommand") {
        // SDK sin.commands.execute()：转发到命令分发（激活监听插件 + 执行）
        onCommandExecuted(params.value("id").toString());
    }
    else if (method == "ai.attach") {
        // Plugin → host: forward into Python Context Inbox (and open AI)
        attachToAi(params);
        if (!id.isUndefined() && m_host)
            m_host->sendResponse(id, QJsonObject{{"ok", true}});
    }
    else {
        spdlog::warn("PluginManager: 未知方法 '{}'", method.toStdString());
        // 请求式未知方法回 JSON-RPC error（方案 §4.5：methodNotFound），
        // 避免 SDK send_request 阻塞到 5s 超时
        if (!id.isUndefined() && m_host)
            m_host->sendErrorResponse(id, -32601,
                                      QStringLiteral("method not found: %1").arg(method));
    }
}

void PluginManager::attachFramesToAi(const QList<CanFrame> &frames, const QString &title)
{
    QJsonArray arr;
    for (const auto &f : frames)
        arr.append(frameToJson(f));
    QJsonObject att{
        {QStringLiteral("kind"), QStringLiteral("trace_frames")},
        {QStringLiteral("title"), title.isEmpty()
             ? QStringLiteral("Trace ×%1").arg(frames.size())
             : title},
        {QStringLiteral("frames"), arr},
    };
    QJsonObject params{{QStringLiteral("attachments"), QJsonArray{att}}};
    attachToAi(params);
}

void PluginManager::attachToAi(const QJsonObject &params)
{
    if (!m_host)
        return;
    // Activate AI agent so the inbox listener is alive
    if (!m_activatedPlugins.contains(QStringLiteral("ai-agent"))) {
        if (m_plugins.contains(QStringLiteral("ai-agent")))
            activatePlugin(QStringLiteral("ai-agent"));
    } else {
        m_host->sendNotification(QStringLiteral("executeCommand"),
                                 QJsonObject{{QStringLiteral("id"),
                                              QStringLiteral("aiAgent.open")}});
    }
    m_host->sendNotification(QStringLiteral("ai.attach"), params);
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

// ============================================================
//  files.* / dbc.* 处理（G9 工具插件 API）
// ============================================================

static CanFileIO::Format convertFormatFromString(const QString &s)
{
    const QString t = s.toLower().trimmed();
    if (t == "blf") return CanFileIO::Format::BLF;
    if (t == "asc") return CanFileIO::Format::ASC;
    if (t == "csv") return CanFileIO::Format::CSV;
    if (t == "pcap") return CanFileIO::Format::PCAP;
    if (t == "trc") return CanFileIO::Format::TRC;
    return CanFileIO::Format::Unknown;
}

void PluginManager::handleFilesConvertStart(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const QString source = params.value("source").toString();
    const QString target = params.value("target").toString();
    const CanFileIO::Format fmt = convertFormatFromString(params.value("format").toString());

    if (source.isEmpty() || target.isEmpty() || fmt == CanFileIO::Format::Unknown) {
        result["error"] = QStringLiteral("参数无效：需要 source/target/format(blf|asc|csv|pcap|trc)");
    } else if (!QFileInfo::exists(source)) {
        result["error"] = QStringLiteral("源文件不存在");
    } else {
        const int jobId = m_nextJobId++;
        auto *job = new PluginConvertJob(source, target, fmt, this);
        m_convertJobs.insert(jobId, job);

        connect(job, &PluginConvertJob::progress, this, [this, jobId](int pct) {
            if (m_host && m_host->isRunning())
                m_host->sendNotification("files.convertProgress",
                                         {{"jobId", jobId}, {"percent", pct}});
        });
        connect(job, &PluginConvertJob::finished, this,
                [this, jobId, job](bool ok, int frameCount, const QString &error) {
            if (m_host && m_host->isRunning())
                m_host->sendNotification("files.convertFinished", {
                    {"jobId", jobId},
                    {"ok", ok},
                    {"frameCount", frameCount},
                    {"error", error}
                });
            m_convertJobs.remove(jobId);
            job->deleteLater();   // job 位于主线程，安全
        });

        job->start();
        result["jobId"] = jobId;
    }

    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleFilesConvertCancel(const QJsonObject &params, const QJsonValue &id)
{
    const int jobId = params.value("jobId").toInt();
    auto it = m_convertJobs.find(jobId);
    if (it != m_convertJobs.end())
        it.value()->requestCancel();
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, QJsonObject{{"ok", it != m_convertJobs.end()}});
}

void PluginManager::handleDbcOpen(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const QString path = params.value("path").toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        result["error"] = QStringLiteral("DBC 文件不存在");
    } else {
        auto *dbc = new DbcFile();
        if (!dbc::parse(path, *dbc)) {
            delete dbc;
            result["error"] = QStringLiteral("DBC 解析失败");
        } else {
            dbc::postProcess(*dbc);
            const int dbId = m_nextDbId++;
            m_dbcSessions.insert(dbId, dbc);
            result["dbId"] = dbId;
            result["messageCount"] = dbc->messages.size();
            result["nodeCount"] = dbc->nodes.size();
            result["version"] = dbc->version;
            result["fileName"] = dbc->fileName.isEmpty()
                                      ? QFileInfo(path).fileName() : dbc->fileName;
        }
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleDbcMessages(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const int dbId = params.value("dbId").toInt();
    auto it = m_dbcSessions.find(dbId);
    if (it == m_dbcSessions.end()) {
        result["error"] = QStringLiteral("会话不存在");
    } else {
        QJsonArray arr;
        for (const auto &msg : it.value()->messages) {
            QJsonObject m;
            m["id"] = static_cast<qint64>(msg.id);
            m["name"] = msg.name;
            m["dlc"] = msg.dlc;
            m["sender"] = msg.sender;
            m["comment"] = msg.comment;
            m["cycleTime"] = msg.cycleTime;
            m["signalCount"] = msg.signalList.size();
            arr.append(m);
        }
        result["messages"] = arr;
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

static QJsonObject signalToJson(const DbcSignal &s)
{
    QJsonObject o;
    o["name"] = s.name;
    o["startBit"] = s.startBit;
    o["bitLength"] = s.bitLength;
    o["littleEndian"] = s.littleEndian;
    o["isSigned"] = s.isSigned;
    o["factor"] = s.factor;
    o["offset"] = s.offset;
    o["min"] = s.minimum;
    o["max"] = s.maximum;
    o["unit"] = s.unit;
    o["receiver"] = s.receiver;
    o["comment"] = s.comment;
    o["muxType"] = static_cast<int>(s.muxType);   // 0=None 1=Multiplexor 2=Multiplexed
    o["muxValue"] = s.muxValue;
    QJsonArray vt;
    for (const auto &e : s.valueTable) {
        QJsonArray pair;
        pair.append(e.value);
        pair.append(e.description);
        vt.append(pair);
    }
    o["valueTable"] = vt;
    return o;
}

void PluginManager::handleDbcSignals(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const int dbId = params.value("dbId").toInt();
    const qint64 msgId = params.value("messageId").toInteger();
    auto it = m_dbcSessions.find(dbId);
    if (it == m_dbcSessions.end()) {
        result["error"] = QStringLiteral("会话不存在");
    } else {
        const DbcMessage *msg = it.value()->findMessage(static_cast<quint32>(msgId));
        if (!msg) {
            result["error"] = QStringLiteral("报文不存在");
        } else {
            QJsonArray arr;
            for (const auto &sig : msg->signalList)
                arr.append(signalToJson(sig));
            result["signals"] = arr;
            result["messageName"] = msg->name;
            result["messageDlc"] = msg->dlc;
        }
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleDbcUpdateSignal(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const int dbId = params.value("dbId").toInt();
    const qint64 msgId = params.value("messageId").toInteger();
    const QString sigName = params.value("name").toString();
    const QJsonObject fields = params.value("fields").toObject();
    auto it = m_dbcSessions.find(dbId);
    if (it == m_dbcSessions.end()) {
        result["error"] = QStringLiteral("会话不存在");
    } else {
        DbcMessage *msg = it.value()->findMessage(static_cast<quint32>(msgId));
        DbcSignal *sig = msg ? msg->findSignal(sigName) : nullptr;
        if (!sig) {
            result["error"] = QStringLiteral("报文或信号不存在");
        } else {
            if (fields.contains("startBit"))  sig->startBit = fields.value("startBit").toInt();
            if (fields.contains("bitLength")) sig->bitLength = fields.value("bitLength").toInt();
            if (fields.contains("littleEndian")) sig->littleEndian = fields.value("littleEndian").toBool();
            if (fields.contains("isSigned"))  sig->isSigned = fields.value("isSigned").toBool();
            if (fields.contains("factor"))    sig->factor = fields.value("factor").toDouble();
            if (fields.contains("offset"))    sig->offset = fields.value("offset").toDouble();
            if (fields.contains("min"))       sig->minimum = fields.value("min").toDouble();
            if (fields.contains("max"))       sig->maximum = fields.value("max").toDouble();
            if (fields.contains("unit"))      sig->unit = fields.value("unit").toString();
            if (fields.contains("comment"))   sig->comment = fields.value("comment").toString();
            result["ok"] = true;
        }
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleDbcUpdateMessage(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const int dbId = params.value("dbId").toInt();
    const qint64 msgId = params.value("messageId").toInteger();
    const QJsonObject fields = params.value("fields").toObject();
    auto it = m_dbcSessions.find(dbId);
    if (it == m_dbcSessions.end()) {
        result["error"] = QStringLiteral("会话不存在");
    } else {
        DbcMessage *msg = it.value()->findMessage(static_cast<quint32>(msgId));
        if (!msg) {
            result["error"] = QStringLiteral("报文不存在");
        } else {
            if (fields.contains("name"))      msg->name = fields.value("name").toString();
            if (fields.contains("dlc"))       msg->dlc = fields.value("dlc").toInt();
            if (fields.contains("comment"))   msg->comment = fields.value("comment").toString();
            if (fields.contains("cycleTime")) msg->cycleTime = fields.value("cycleTime").toInt();
            result["ok"] = true;
        }
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleDbcSave(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const int dbId = params.value("dbId").toInt();
    QString path = params.value("path").toString();
    auto it = m_dbcSessions.find(dbId);
    if (it == m_dbcSessions.end()) {
        result["error"] = QStringLiteral("会话不存在");
    } else {
        DbcFile *dbc = it.value();
        if (path.isEmpty())
            path = dbc->filePath;
        if (path.isEmpty()) {
            result["error"] = QStringLiteral("未指定保存路径");
        } else if (!dbc::write(path, *dbc)) {
            result["error"] = QStringLiteral("写入 DBC 失败");
        } else {
            dbc->filePath = path;   // 另存为后更新会话路径
            result["ok"] = true;
        }
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleDbcClose(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const int dbId = params.value("dbId").toInt();
    auto it = m_dbcSessions.find(dbId);
    if (it != m_dbcSessions.end()) {
        delete it.value();
        m_dbcSessions.erase(it);
        result["ok"] = true;
    } else {
        result["error"] = QStringLiteral("会话不存在");
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

// ============================================================
//  signals.* / workspace.* 处理（方案 §4.4 控制链路方法补齐）
//  — signals.* 基于工程内已加载 DBC（DbcManager），与插件独立的
//    dbc.* 会话（G9）互补：前者解码当前工程总线数据，后者编辑文件
// ============================================================

void PluginManager::handleSignalsDecode(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const quint32 canId = static_cast<quint32>(params.value("id").toVariant().toUInt());
    const QByteArray data = QByteArray::fromHex(
        params.value("data").toString().toUtf8());

    if (!m_dbcManager) {
        result["error"] = QStringLiteral("DBC 管理器未注入");
    } else if (!m_dbcManager->findMessage(canId)) {
        result["error"] = QStringLiteral("无匹配 DBC 报文定义");
    } else {
        QJsonObject sigValues;
        const auto decoded = m_dbcManager->decodeFrame(canId, data);
        for (const auto &ds : decoded)
            sigValues[ds.name] = ds.physValue;
        result["signals"] = sigValues;
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleSignalsEncode(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const quint32 canId = static_cast<quint32>(params.value("id").toVariant().toUInt());
    const QJsonObject values = params.value("signals").toObject();

    if (!m_dbcManager) {
        result["error"] = QStringLiteral("DBC 管理器未注入");
    } else if (const DbcMessage *msg = m_dbcManager->findMessage(canId); msg) {
        int len = msg->dlc;
        if (len <= 0)
            len = 8;
        QByteArray data(len, 0x00);
        int encoded = 0;
        for (auto it = values.begin(); it != values.end(); ++it) {
            const DbcSignal *sig = m_dbcManager->findSignal(canId, it.key());
            if (!sig)
                continue;
            sig->encode(data, it.value().toDouble());
            ++encoded;
        }
        if (encoded == 0) {
            result["error"] = QStringLiteral("未找到匹配信号");
        } else {
            result["data"] = QString::fromUtf8(data.toHex());
        }
    } else {
        result["error"] = QStringLiteral("无匹配 DBC 报文定义");
    }
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleWorkspaceProjectDir(const QJsonObject &params, const QJsonValue &id)
{
    Q_UNUSED(params);
    QJsonObject result;
    const QString filePath = ProjectManager::instance()->currentFilePath();
    // 工程目录 = 工程文件所在目录；未打开工程时为空
    result["path"] = filePath.isEmpty() ? QString()
                                        : QFileInfo(filePath).absolutePath();
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleWorkspaceDbcFiles(const QJsonObject &params, const QJsonValue &id)
{
    Q_UNUSED(params);
    QJsonObject result;
    QJsonArray files;
    if (m_dbcManager) {
        for (const auto &f : m_dbcManager->files()) {
            if (!f.filePath.isEmpty())
                files.append(f.filePath);
        }
    }
    result["files"] = files;
    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

void PluginManager::handleWorkspaceGetSetting(const QJsonObject &params, const QJsonValue &id)
{
    QJsonObject result;
    const QString key = params.value("key").toString();
    const QJsonValue def = params.value("default");

    // 按默认值类型分派 typed getter（AppConfig 无原始 QVariant 读取）
    if (def.isBool())
        result["value"] = AppConfig::instance()->getBool(key, def.toBool());
    else if (def.isDouble())
        result["value"] = AppConfig::instance()->getDouble(key, def.toDouble());
    else if (def.isString())
        result["value"] = AppConfig::instance()->getString(key, def.toString());
    else
        result["value"] = AppConfig::instance()->getString(key);

    if (!id.isUndefined() && m_host)
        m_host->sendResponse(id, result);
}

// ============================================================
//  插件包安装/卸载（G9 .opk，具体 zip 操作委托 scripts/plugin_tool.py）
// ============================================================

QString PluginManager::installPackage(const QString &opkPath)
{
    if (!QFileInfo::exists(opkPath))
        return QStringLiteral("插件包不存在: %1").arg(opkPath);
    if (m_pythonExe.isEmpty())
        m_pythonExe = findPythonExecutable();
    if (m_pythonExe.isEmpty())
        return QStringLiteral("未找到 Python 解释器");

    const QString toolPath = QFileInfo(m_hostScriptPath).dir().filePath("plugin_tool.py");
    if (!QFileInfo::exists(toolPath))
        return QStringLiteral("打包工具不存在: %1").arg(toolPath);

    QProcess proc;
    proc.start(m_pythonExe, {toolPath, "install", opkPath, installPluginsDir()});
    if (!proc.waitForFinished(60000)) {
        proc.kill();
        return QStringLiteral("安装超时");
    }
    const QByteArray out = proc.readAllStandardOutput().trimmed();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QStringLiteral("安装脚本输出异常: %1")
                   .arg(QString::fromUtf8(out).left(300));

    const QJsonObject res = doc.object();
    if (!res.value("ok").toBool())
        return res.value("error").toString(QStringLiteral("安装失败"));

    // 重新扫描并确保宿主运行（此前无插件时宿主未启动）
    discoverPlugins();
    startHostIfNeeded();
    emit pluginListChanged();
    spdlog::info("PluginManager: 已安装插件 '{}'",
                 res.value("name").toString().toStdString());
    return QString();
}

QString PluginManager::uninstallPlugin(const QString &name)
{
    if (!m_plugins.contains(name))
        return QStringLiteral("插件不存在: %1").arg(name);
    if (m_disabledPlugins.contains(name) == false)
        setPluginEnabled(name, false);   // 内部会先停用
    else
        deactivatePlugin(name);

    const QString toolPath = QFileInfo(m_hostScriptPath).dir().filePath("plugin_tool.py");
    QProcess proc;
    proc.start(m_pythonExe, {toolPath, "uninstall", name, installPluginsDir()});
    if (!proc.waitForFinished(30000)) {
        proc.kill();
        return QStringLiteral("卸载超时");
    }
    const QByteArray out = proc.readAllStandardOutput().trimmed();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(out, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return QStringLiteral("卸载脚本输出异常: %1")
                   .arg(QString::fromUtf8(out).left(300));
    const QJsonObject res = doc.object();
    if (!res.value("ok").toBool())
        return res.value("error").toString(QStringLiteral("卸载失败"));

    discoverPlugins();
    emit pluginListChanged();
    spdlog::info("PluginManager: 已卸载插件 '{}'", name.toStdString());
    return QString();
}
