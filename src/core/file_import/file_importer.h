#ifndef FILE_IMPORTER_H
#define FILE_IMPORTER_H

#include <QVector>
#include <QString>
#include <QStringList>
#include <memory>
#include <functional>
#include "core/canframe.h"

/**
 * @brief 统一文件导入接口
 *
 * 所有格式导入器（ASC/BLF/CSV）实现此接口。
 * 导入为只读操作，返回 CanFrame 序列。
 */
class FileImporter
{
public:
    virtual ~FileImporter() = default;

    /// 导入文件，返回帧序列
    /// @param filePath 文件路径
    /// @param progress 进度回调 (0.0~1.0)，可为 nullptr
    /// @return 导入的帧序列，失败返回空
    virtual QVector<CanFrame> importFile(
        const QString &filePath,
        std::function<void(double)> progress = nullptr) = 0;

    /// 支持的文件扩展名（小写，不含点）
    virtual QStringList supportedExtensions() const = 0;

    /// 格式显示名称
    virtual QString formatName() const = 0;
};

/**
 * @brief 文件导入工厂 — 根据扩展名自动选择导入器
 */
class FileImportFactory
{
public:
    /// 根据文件扩展名创建合适的导入器
    /// @return 导入器实例，不支持的格式返回 nullptr
    static std::unique_ptr<FileImporter> create(const QString &filePath);

    /// 获取所有支持的文件过滤器（用于 QFileDialog）
    static QStringList fileFilters();
};

#endif // FILE_IMPORTER_H
