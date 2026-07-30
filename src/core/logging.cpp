#include "logging.h"

#include <QStandardPaths>
#include <QDir>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace logging {

static std::shared_ptr<spdlog::logger> s_logger;

void init(const QString& logDir)
{
    // 确定日志目录
    QString dir = logDir;
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!dir.isEmpty()) {
            dir += "/logs";
        }
    }
    QDir().mkpath(dir);

    // 创建 sink
    std::vector<spdlog::sink_ptr> sinks;

    // 控制台 sink（彩色输出）
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_level(spdlog::level::debug);
    sinks.push_back(consoleSink);

    // 滚动文件 sink（5MB x 3 个文件）
    // 若指定目录不可写，回退到当前目录下的 logs/
    QString logFile = dir + "/sin.log";
    try {
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFile.toStdString(),
            5 * 1024 * 1024,  // 5 MB
            3                  // 3 个滚动文件
        );
        fileSink->set_level(spdlog::level::trace);
        sinks.push_back(fileSink);
    } catch (const spdlog::spdlog_ex&) {
        // 回退到当前目录
        dir = "./logs";
        QDir().mkpath(dir);
        logFile = dir + "/sin.log";
        try {
            auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logFile.toStdString(),
                5 * 1024 * 1024,
                3
            );
            fileSink->set_level(spdlog::level::trace);
            sinks.push_back(fileSink);
        } catch (const spdlog::spdlog_ex&) {
            // 文件 sink 不可用，仅使用控制台
        }
    }

    // 创建默认日志器
    s_logger = std::make_shared<spdlog::logger>("sin", sinks.begin(), sinks.end());
    s_logger->set_level(spdlog::level::debug);
    s_logger->flush_on(spdlog::level::info);
    s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    spdlog::set_default_logger(s_logger);
    spdlog::set_level(spdlog::level::debug);

    SPDLOG_INFO("日志系统初始化完成，日志目录: {}", dir.toStdString());
}

void shutdown()
{
    if (s_logger) {
        s_logger->flush();
    }
    spdlog::shutdown();
    s_logger.reset();
}

spdlog::logger* logger()
{
    return s_logger.get();
}

} // namespace logging
