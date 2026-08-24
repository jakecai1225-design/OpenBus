#include "pluginmanager.h"
#include "pluginhost.h"
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

QString PluginManager::findPythonExecutable() const
{
    // 按优先级查找 Python 解释器
    QStringList candidates;

    // 环境变量指定的 Python
    QString envPython = QProcessEnvironment::systemEnvironment().value("SIN_PYTHON");
    if (!envPython.isEmpty())
        candidates << envPython;

    // 捆绑运行时 Python（<exeDir>/runtime/python/python.exe）——
    // 便携版/安装包自带（打包方案 §6.1），优先于系统 Python；
    // SIN_PYTHON 环境变量仍最高优先，便于用户替换自带解释器
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("runtime/python/python.exe"));
    if (QFileInfo::exists(bundled))
        candidates << QDir::toNativeSeparators(bundled);

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
    if (m_host)
        return;   // 宿主已运行（如安装首个插件后补启）

    if (m_plugins.isEmpty()) {
        spdlog::info("PluginManager: 无插件，跳过宿主启动");
        return;
    }

    if (m_pythonExe.isEmpty())
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

    // 单例有意泄漏不析构（见 instance()，DEF-07）：Python 宿主改经
    // qApp aboutToQuit 优雅关闭（事件循环仍在 → QProcess I/O 合法），
    // 避免 DLL 卸载阶段静态析构触发加载器锁崩溃
    if (qApp)
        connect(qApp, &QCoreApplication::aboutToQuit,
                this, &PluginManager::shutdown);

    connect(m_host, &PluginHost::messageReceived,
            this, &PluginManager::handleHostMessage);
    connect(m_host, &PluginHost::hostStarted,
            this, &PluginManager::onHostStarted);
    connect(m_host, &PluginHost::hostCrashed, []() {
        spdlog::warn("PluginManager: 插件宿主崩溃");
    });

    if (!m_host->start(m_pythonExe, m_hostScriptPath, m_sdkDir, m_pluginsDir)) {
        spdlog::error("PluginManager: 插件宿主启动失败");
        return;
    }

    // 启动帧批量定时器（100ms）
    if (!m_frameBatchTimerId)
        m_frameBatchTimerId = startTimer(100);
}

void PluginManager::onHostStarted()
{
    // 崩溃自愈（方案 §一 2）：宿主重启后是空壳进程，订阅表失效、
    // 已激活插件需重新发送 activate 通知（activate() 内重新注册 on_frame
    // 回调 → 重新 subscribeFrames，数据链路自动恢复）。
    // 首次启动时 m_activatedPlugins 为空 → 无操作。
    if (!m_frameSubscribers.isEmpty())
        spdlog::info("PluginManager: 宿主重启，重建数据链路订阅");
    m_frameSubscribers.clear();

    if (m_activatedPlugins.isEmpty())
        return;

    spdlog::info("PluginManager: 宿主重启，重新激活 {} 个插件",
                 m_activatedPlugins.size());
    const QStringList names = m_activatedPlugins.values();
    for (const auto &name : names) {
        if (!m_plugins.contains(name))
            continue;
        const PluginInfo &info = m_plugins[name];
        QJsonObject params;
        params["plugin"] = name;
        params["directory"] = info.directory;
        params["main"] = info.mainScript;
        m_host->sendNotification("activate", params);
        spdlog::info("PluginManager: 重新激活插件 '{}'", name.toStdString());
    }
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

    // 已激活的插件先停用再激活（重新创建 UI 窗口等）
    if (m_activatedPlugins.contains(name))
        deactivatePlugin(name);
    activatePlugin(name);

    emit pluginListChanged();
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
    m_frameSubscribers.remove(name);

    spdlog::info("PluginManager: 停用插件 '{}'", name.toStdString());
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
            // 分块发送（每批 ≤100 帧，方案 §二），避免单行 JSON 过大
            const int total = m_frameBuffer.size();
            for (int off = 0; off < total; off += kFramesPerBatch) {
                const int end = qMin(total, off + kFramesPerBatch);
                QJsonArray framesArray;
                for (int i = off; i < end; ++i)
                    framesArray.append(frameToJson(m_frameBuffer.at(i)));
                m_host->sendNotification("frameReceived", {{"frames", framesArray}});
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
    else {
        spdlog::warn("PluginManager: 未知方法 '{}'", method.toStdString());
        // 请求式未知方法回 JSON-RPC error（方案 §4.5：methodNotFound），
        // 避免 SDK send_request 阻塞到 5s 超时
        if (!id.isUndefined() && m_host)
            m_host->sendErrorResponse(id, -32601,
                                      QStringLiteral("method not found: %1").arg(method));
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
    proc.start(m_pythonExe, {toolPath, "install", opkPath, m_pluginsDir});
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
    proc.start(m_pythonExe, {toolPath, "uninstall", name, m_pluginsDir});
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
