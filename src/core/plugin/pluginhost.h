#ifndef PLUGINHOST_H
#define PLUGINHOST_H

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <functional>

class QProcess;
class QTimer;

/**
 * @brief Python 插件宿主进程管理 — QProcess + JSON-RPC over stdin/stdout
 *
 * 负责：
 * - 启动/停止 Python 子进程 (sin_host.py)
 * - 通过 newline-delimited JSON 进行双向通信
 * - 管理异步请求的回调
 * - 崩溃后自动重启（指数退避）
 */
class PluginHost : public QObject
{
    Q_OBJECT

public:
    explicit PluginHost(QObject *parent = nullptr);
    ~PluginHost();

    /// 启动 Python 宿主进程
    /// @param pythonExe Python 解释器路径
    /// @param hostScript sin_host.py 的完整路径
    /// @param sdkDir SDK 目录路径（设置 SIN_SDK_DIR 环境变量）
    /// @param pluginsDir 插件根目录路径（设置 SIN_PLUGINS_DIR 环境变量）
    bool start(const QString &pythonExe, const QString &hostScript,
               const QString &sdkDir, const QString &pluginsDir);

    /// 停止宿主进程
    void stop();

    /// 是否正在运行
    bool isRunning() const;

    // ---- JSON-RPC 发送 ----

    /// 发送通知（无需回复）
    void sendNotification(const QString &method, const QJsonObject &params = {});

    /// 发送请求（需回复，异步回调）
    void sendRequest(const QString &method, const QJsonObject &params,
                     std::function<void(const QJsonValue &)> callback);

    /// 发送请求（无需回调）
    void sendRequest(const QString &method, const QJsonObject &params = {});

    /// 发送响应（回复 Python 宿主的请求）
    void sendResponse(const QJsonValue &id, const QJsonValue &result);

signals:
    /// 收到来自 Python 宿主的通知/请求
    void messageReceived(const QString &method, const QJsonObject &params, const QJsonValue &id);

    /// 宿主进程已启动
    void hostStarted();

    /// 宿主进程崩溃
    void hostCrashed();

    /// 宿主进程错误
    void hostError(const QString &error);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, int exitStatus);
    void onProcessError(int error);
    void onRestartTimer();

private:
    QProcess *m_process = nullptr;
    QByteArray m_readBuffer;         ///< 不完整行缓冲
    int m_nextRequestId = 1;
    QHash<int, std::function<void(const QJsonValue &)>> m_pendingRequests;
    QTimer *m_restartTimer = nullptr;
    int m_restartAttempts = 0;       ///< 当前重启尝试次数

    // 启动参数缓存（用于重启）
    QString m_pythonExe;
    QString m_hostScript;
    QString m_sdkDir;
    QString m_pluginsDir;

    void sendMessage(const QJsonObject &msg);
    void handleLine(const QByteArray &line);
    void scheduleRestart();
};

#endif // PLUGINHOST_H
