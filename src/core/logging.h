#ifndef LOGGING_H
#define LOGGING_H

#include <spdlog/spdlog.h>

#include <QString>

/**
 * @brief 日志系统初始化与宏定义
 *
 * 基于 spdlog，提供控制台 + 滚动文件双 sink。
 * 使用 SIN_LOG_* 系列宏替代 qDebug，格式：
 *   [2026-07-29 14:30:00.123] [info] [DbcManager] message
 */

namespace logging {

/// 初始化全局日志器（控制台 + 滚动文件）
/// @param logDir 日志目录，为空则使用 QStandardPaths::AppDataLocation/logs
void init(const QString& logDir = QString());

/// 关闭日志系统，刷新缓冲
void shutdown();

/// 获取默认日志器
spdlog::logger* logger();

} // namespace logging

// ============================================================
//  日志宏 — tag 标识模块，fmt 格式化
// ============================================================
// 用法: SIN_LOG_INFO("DbcManager", "解析文件: {}", fileName.toStdString());

#define SIN_LOG_DEBUG(tag, ...) \
    SPDLOG_DEBUG("[{}] {}", tag, fmt::format(__VA_ARGS__))

#define SIN_LOG_INFO(tag, ...) \
    SPDLOG_INFO("[{}] {}", tag, fmt::format(__VA_ARGS__))

#define SIN_LOG_WARN(tag, ...) \
    SPDLOG_WARN("[{}] {}", tag, fmt::format(__VA_ARGS__))

#define SIN_LOG_ERROR(tag, ...) \
    SPDLOG_ERROR("[{}] {}", tag, fmt::format(__VA_ARGS__))

#endif // LOGGING_H
