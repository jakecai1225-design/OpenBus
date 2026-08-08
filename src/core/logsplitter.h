#ifndef LOGSPLITTER_H
#define LOGSPLITTER_H

#include <QObject>
#include <QString>
#include <QQueue>
#include <memory>
#include "core/canframe.h"

class CanFileWriter;

/**
 * @brief 日志文件分片管理器（对标 CANoe Logging Block）
 *
 * 功能：
 *   - 按文件大小自动切割（达到阈值时关闭当前文件，打开新文件）
 *   - 按时间自动切割（达到配置时长时切割）
 *   - 环形模式：达到最大文件数时覆盖最旧文件
 *   - 文件名追加序号：prefix_001.blf, prefix_002.blf, ...
 */
class LogSplitter : public QObject
{
    Q_OBJECT

public:
    struct Config {
        QString directory;       ///< 录制目录
        QString prefix;          ///< 文件名前缀
        QString format = "blf";  ///< 文件格式 (blf/asc/csv)
        bool splitBySize = true;
        quint64 maxSizeBytes = 50 * 1024 * 1024;  ///< 50 MB 默认
        bool splitByTime = false;
        double maxTimeSeconds = 600.0;           ///< 10 分钟默认
        bool ringMode = false;                  ///< 环形模式
        int maxFiles = 10;                       ///< 环形模式下最大文件数
    };

    explicit LogSplitter(QObject *parent = nullptr);
    ~LogSplitter();

    bool start(const Config &config);
    void stop();

    bool isRecording() const { return m_recording; }

public slots:
    void recordFrame(const CanFrame &frame);

signals:
    void fileSplit(const QString &oldFile, const QString &newFile);
    void frameRecorded(int totalFrames);

private:
    bool m_recording = false;
    Config m_config;
    std::unique_ptr<CanFileWriter> m_writer;
    QString m_currentPath;
    int m_currentSeq = 0;
    quint64 m_currentSize = 0;
    double m_fileStartTime = 0.0;
    int m_frameCount = 0;

    bool openNewFile();
    void closeCurrentFile();
    QString generateFileName(int seq) const;

    /// 检查是否需要分片
    bool shouldSplit(double timestamp) const;

    /// 环形模式下删除最旧文件
    void pruneOldFiles();
};

#endif // LOGSPLITTER_H
