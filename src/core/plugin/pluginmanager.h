#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "plugininfo.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QList>
#include <functional>

class PluginHost;
class PluginConvertJob;
struct DbcFile;
class DbcManager;
class CanFrame;
class QJsonObject;
class QJsonValue;
struct CanFrame;

/**
 * @brief 插件管理器 — 单例，负责插件发现、激活、停用和消息分发
 *
 * 架构：
 * - discoverPlugins() 扫描 plugins/ 目录，加载所有 plugin.json
 * - initialize() 启动 Python 宿主进程（不自动激活插件，由用户双击触发）
 * - onFrameReceived() 将帧转发给已激活的 onFrame 插件
 * - 主程序通过信号接收来自插件的操作请求（发送帧、输出文本等）
 */
class PluginManager : public QObject
{
    Q_OBJECT

public:
    static PluginManager *instance();

    /// 初始化：查找 Python、扫描插件目录、启动宿主
    void initialize();

    /// 关闭：停用所有插件、停止宿主进程
    void shutdown();

    // ---- 插件发现 ----

    QList<PluginInfo> discoveredPlugins() const;
    bool isPluginEnabled(const QString &name) const;
    bool isPluginActivated(const QString &name) const;
    void setPluginEnabled(const QString &name, bool enabled);

    /// 双击插件项时调用：已激活则先停用再激活，未激活则直接激活
    void reactivatePlugin(const QString &name);

    /// 宿主进程是否运行
    bool isHostRunning() const;

    /// 宿主进程 PID（未运行返回 0）
    qint64 hostProcessId() const;

    /// Python interpreter path (cached after host start)
    QString pythonExecutable() const { return m_pythonExe; }

    /// Resolve MSYS2 UCRT64 Python with PyQt6 + pyzmq (plugin host requirement).
    /// Order: SIN_PYTHON → MSYSTEM_PREFIX/MINGW_PREFIX → common msys64 roots →
    /// optional bundled runtime. Bare PATH `python`/`py` is rejected unless it
    /// resolves under an MSYS2 *64 prefix and imports PyQt6 + zmq.
    static QString resolvePluginPython();

    /// Inject project DBC manager (signals.* decode/encode; consumer only)
    /// MainWindow 创建 DbcManager 后注入；未注入时 signals.* 返回错误
    void setDbcManager(DbcManager *mgr) { m_dbcManager = mgr; }

    // ---- 激活事件（由 MainWindow 调用）----

    void onFrameReceived(const CanFrame &frame);
    void onCommandExecuted(const QString &commandId);
    void onFileOpened(const QString &path, const QString &extension);
    void onStartup();

    // ---- 主程序 → 插件宿主 ----

    /// 激活指定插件
    void activatePlugin(const QString &name);

    /// 停用指定插件
    void deactivatePlugin(const QString &name);

    /// 执行插件命令
    void executeCommand(const QString &commandId);

signals:
    // ---- 插件 → 主程序（由 PluginHost 转发）----

    /// 插件请求输出文本到面板
    void outputMessage(const QString &text);

    /// 插件注册了命令
    void commandRegistered(const QString &id, const QString &title);

    /// 插件请求发送 CAN 帧
    void sendFrameRequested(const CanFrame &frame);

    /// 插件请求清空输出
    void outputClearRequested();

    /// 插件列表变化
    void pluginListChanged();

    /// 插件日志
    void pluginLog(int level, const QString &message);

    /// Python 宿主请求获取选中帧（MainWindow 应连接此信号，调用 provideSelectedFrames 回复）
    void requestSelectedFrames(const QJsonValue &requestId);

    /// Python 宿主请求获取最近帧
    void requestRecentFrames(const QJsonValue &requestId, int count);

public:
    /// Attach Trace frames (or generic JSON attachments) to the AI Agent chat.
    void attachToAi(const QJsonObject &params);
    void attachFramesToAi(const QList<CanFrame> &frames, const QString &title = QString());

    /// MainWindow 调用：提供选中帧数据回复 Python 宿主
    void provideSelectedFrames(const QJsonValue &requestId, const QList<CanFrame> &frames);

    /// MainWindow 调用：提供最近帧数据回复 Python 宿主
    void provideRecentFrames(const QJsonValue &requestId, const QList<CanFrame> &frames);

    // ---- 插件包安装/卸载（G9 .opk）----

    /// 从 .opk 包安装插件
    /// @return 错误信息，空串表示成功
    QString installPackage(const QString &opkPath);

    /// 卸载插件（停用并删除 plugins/<name>/ 目录）
    /// @return 错误信息，空串表示成功
    QString uninstallPlugin(const QString &name);

private:
    PluginManager(QObject *parent = nullptr);

    PluginHost *m_host = nullptr;
    QHash<QString, PluginInfo> m_plugins;     ///< name → info
    QSet<QString> m_activatedPlugins;         ///< 已激活的插件名
    QSet<QString> m_disabledPlugins;          ///< 用户禁用的插件名

    QString m_pluginsDir;                     ///< plugins/ 目录
    QString m_sdkDir;                         ///< sdk/ 目录
    QString m_hostScriptPath;                 ///< sin_host.py 路径
    QString m_pythonExe;                      ///< Python 解释器路径

    // files.* 转换 Job 表（G9）
    QHash<int, PluginConvertJob *> m_convertJobs;
    int m_nextJobId = 1;

    // dbc.* 会话表（G9）：dbId → 插件独立打开的 DBC 文件（与工程内 DbcManager 无关）
    QHash<int, DbcFile *> m_dbcSessions;
    int m_nextDbId = 1;

    // 工程内 DBC 管理器（signals.* 用，MainWindow 注入，不持有）
    DbcManager *m_dbcManager = nullptr;

    // 帧批量缓冲（订阅制：仅当存在已注册帧回调的已激活插件时才缓冲）
    QList<CanFrame> m_frameBuffer;
    int m_frameBatchTimerId = 0;

    // 数据链路订阅表（方案 §一 5.1 订阅制的 v1 落地）：
    // 插件 activate() 中 context.on_frame() 注册首个回调时，宿主发送
    // subscribeFrames 通知登记于此；无订阅 = 零开销（不缓冲、不序列化）
    QSet<QString> m_frameSubscribers;

    // 声明 onFrame 激活事件的插件（懒激活候选，discoverPlugins 重算）
    QSet<QString> m_onFramePlugins;

    // Activations requested before host.hello — flushed in onHostStarted()
    QStringList m_pendingActivations;

    // 慢消费者保护：缓冲上限丢弃计数（flush 时告警并清零）
    int m_droppedFrames = 0;

    void discoverPlugins();
    QString findPythonExecutable() const;
    QString findAppBaseDir() const;
    void startHostIfNeeded();   ///< 无插件时跳过；安装首个插件后可补启
    void onHostStarted();       ///< 崩溃自愈：重启后重激活已激活插件（首启无操作）

    /// True when plugin dir has app_shell.py (shared import names across domains).
    bool usesSharedSuiteModules(const QString &name) const;

    // 处理来自 Python 宿主的消息
    void handleHostMessage(const QString &method, const QJsonObject &params, const QJsonValue &id);

    // files.* / dbc.* 请求处理（G9）
    void handleFilesConvertStart(const QJsonObject &params, const QJsonValue &id);
    void handleFilesConvertCancel(const QJsonObject &params, const QJsonValue &id);
    void handleDbcOpen(const QJsonObject &params, const QJsonValue &id);
    void handleDbcMessages(const QJsonObject &params, const QJsonValue &id);
    void handleDbcSignals(const QJsonObject &params, const QJsonValue &id);
    void handleDbcUpdateSignal(const QJsonObject &params, const QJsonValue &id);
    void handleDbcUpdateMessage(const QJsonObject &params, const QJsonValue &id);
    void handleDbcSave(const QJsonObject &params, const QJsonValue &id);
    void handleDbcClose(const QJsonObject &params, const QJsonValue &id);

    // signals.* / workspace.* 请求处理（方案 §4.4 控制链路方法补齐）
    void handleSignalsDecode(const QJsonObject &params, const QJsonValue &id);
    void handleSignalsEncode(const QJsonObject &params, const QJsonValue &id);
    void handleWorkspaceProjectDir(const QJsonObject &params, const QJsonValue &id);
    void handleWorkspaceDbcFiles(const QJsonObject &params, const QJsonValue &id);
    void handleWorkspaceGetSetting(const QJsonObject &params, const QJsonValue &id);

protected:
    void timerEvent(QTimerEvent *event) override;
};

#endif // PLUGINMANAGER_H
