#ifndef BLF_IMPORTER_H
#define BLF_IMPORTER_H

#include "core/file_import/file_importer.h"

/**
 * @brief Vector BLF (Binary Logging Format) 文件导入器
 *
 * 解析 Vector BLF 二进制日志文件，支持：
 * - 经典 CAN 报文（CAN_MESSAGE, type 1 / CAN_MESSAGE2, type 86）
 * - CAN FD 报文（CAN_FD_MESSAGE, type 100 / CAN_FD_MESSAGE_64, type 101）
 * - zlib 压缩的 Log Container
 * - 标准帧 / 扩展帧
 * - BRS / ESI 标志
 *
 * BLF 时间戳为 1/10 微秒精度，以 2007-01-01 为起点。
 * 导入后转换为相对秒。
 */
class BlfImporter : public FileImporter
{
public:
    QVector<CanFrame> importFile(
        const QString &filePath,
        std::function<void(double)> progress = nullptr) override;

    QStringList supportedExtensions() const override { return {"blf"}; }
    QString formatName() const override { return QStringLiteral("Vector BLF"); }
};

#endif // BLF_IMPORTER_H
