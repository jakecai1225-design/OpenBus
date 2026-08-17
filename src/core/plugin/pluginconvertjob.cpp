#include "pluginconvertjob.h"
#include "core/canframe.h"
#include "core/canfileio/canfileio_factory.h"

#include <QThread>
#include <QVector>

PluginConvertJob::PluginConvertJob(const QString &source, const QString &target,
                                   CanFileIO::Format fmt, QObject *parent)
    : QObject(parent)
    , m_source(source)
    , m_target(target)
    , m_fmt(fmt)
{
}

PluginConvertJob::~PluginConvertJob()
{
    // 确保工作线程先于本对象结束（run() 在 emit finished 后立即返回，等待极短）
    if (m_thread) {
        m_thread->wait(3000);
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

void PluginConvertJob::start()
{
    m_cancel = false;
    // QThread::create：函数体在新线程执行；progress/finished 跨线程排队到主线程
    m_thread = QThread::create([this]() { run(); });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void PluginConvertJob::requestCancel()
{
    m_cancel = true;
}

void PluginConvertJob::run()
{
    const QString source = m_source;   // 拷贝到栈上，避免与析构竞态
    const QString target = m_target;
    const CanFileIO::Format fmt = m_fmt;

    QVector<CanFrame> frames;
    int total = 0;

    // ---- 读取 ----
    auto reader = CanFileIOFactory::createReader(source);
    if (!reader || !reader->open(source)) {
        emit finished(false, 0, QStringLiteral("无法打开源文件: %1").arg(source));
        return;
    }
    total = reader->readAll(frames);
    reader->close();
    if (m_cancel) {
        emit finished(false, 0, QStringLiteral("转换已取消"));
        return;
    }
    if (total < 0) {
        emit finished(false, 0, QStringLiteral("读取源文件失败"));
        return;
    }
    if (frames.isEmpty()) {
        emit finished(false, 0, QStringLiteral("源文件中无有效报文帧"));
        return;
    }

    // ---- 写入 ----
    auto writer = CanFileIOFactory::createWriter(fmt);
    if (!writer) {
        emit finished(false, 0, QStringLiteral("目标格式不支持写入"));
        return;
    }
    if (!writer->open(target)) {
        emit finished(false, 0, QStringLiteral("无法创建目标文件: %1").arg(target));
        return;
    }
    for (int i = 0; i < frames.size(); ++i) {
        if (m_cancel) {
            writer->close();
            emit finished(false, 0, QStringLiteral("转换已取消"));
            return;
        }
        writer->writeFrame(frames[i]);
        if ((i % 500) == 0)
            emit progress(i * 100 / qMax(1, frames.size()));
    }
    writer->close();
    emit progress(100);
    emit finished(true, total, QString());
}
