#ifndef CANFILEIO_FACTORY_H
#define CANFILEIO_FACTORY_H

#include "canfileio.h"
#include <memory>

/**
 * @brief 文件格式工厂 — 根据扩展名创建对应的读写器
 */
class CanFileIOFactory
{
public:
    /// 创建写入器（根据文件路径的扩展名）
    /// @return 写入器指针，不支持的格式返回 nullptr
    static std::unique_ptr<CanFileWriter> createWriter(const QString &filePath);

    /// 创建写入器（根据格式枚举）
    static std::unique_ptr<CanFileWriter> createWriter(CanFileIO::Format fmt);

    /// 创建读取器（根据文件路径的扩展名）
    /// @return 读取器指针，不支持的格式返回 nullptr
    static std::unique_ptr<CanFileReader> createReader(const QString &filePath);

    /// 创建读取器（根据格式枚举）
    static std::unique_ptr<CanFileReader> createReader(CanFileIO::Format fmt);

    /// 判断给定扩展名是否支持写入
    static bool canWrite(const QString &suffix);

    /// 判断给定扩展名是否支持读取
    static bool canRead(const QString &suffix);
};

#endif // CANFILEIO_FACTORY_H
