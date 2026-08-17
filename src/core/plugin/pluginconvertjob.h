#ifndef PLUGINCONVERTJOB_H
#define PLUGINCONVERTJOB_H

#include <QObject>
#include <atomic>
#include "core/canfileio/canfileio.h"

class QThread;

/**
 * @brief 插件格式转换后台任务 — files.convert API 的 C++ 侧执行体（G9）
 *
 * 在独立线程中完成 源文件读取 → 目标格式写入 全流程，
 * 进度与结果通过信号回报，由 PluginManager 转发为 JSON-RPC 通知。
 *
 * 复用 CanFileIOFactory 读写器（BLF/ASC/CSV/PCAP/TRC），
 * 转换逻辑与原 BlfAsConverter::ConvertWorker 等价。
 */
class PluginConvertJob : public QObject
{
    Q_OBJECT
public:
    explicit PluginConvertJob(const QString &source, const QString &target,
                              CanFileIO::Format fmt, QObject *parent = nullptr);
    ~PluginConvertJob() override;

    /// 启动后台转换线程
    void start();

    /// 请求取消（置中断标志，工作循环在下一个帧边界退出）
    void requestCancel();

signals:
    void progress(int percent);
    void finished(bool ok, int frameCount, const QString &error);

private:
    void run();   // 在工作线程中执行

    QString m_source;
    QString m_target;
    CanFileIO::Format m_fmt;
    QThread *m_thread = nullptr;
    std::atomic_bool m_cancel{false};
};

#endif // PLUGINCONVERTJOB_H
