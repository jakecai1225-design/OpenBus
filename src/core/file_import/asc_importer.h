#ifndef ASC_IMPORTER_H
#define ASC_IMPORTER_H

#include "core/file_import/file_importer.h"

/**
 * @brief Vector ASC (ASCII Logging) 文件导入器
 *
 * 解析 Vector ASC 格式日志文件，支持：
 * - 经典 CAN 报文（标准帧 / 扩展帧）
 * - CAN FD 报文（含 BRS 标志）
 * - 远程帧
 * - 错误帧
 * - 新格式（CANoe 7.x+）和旧格式（带 CAN/CANFD 关键字）
 *
 * ASC 格式示例（新格式）：
 *   0.001000 1  200             Rx   d 8 01 02 03 04 05 06 07 08
 *   0.002000 1  0x456x          Rx   D 8 AA BB CC DD EE FF 00 11
 *   0.003000 1  0x789           Rx   fd 64 01 02 ...
 *   0.004000 1  0x789           Rx   FD 64 01 02 ...
 */
class AscImporter : public FileImporter
{
public:
    QVector<CanFrame> importFile(
        const QString &filePath,
        std::function<void(double)> progress = nullptr) override;

    QStringList supportedExtensions() const override { return {"asc"}; }
    QString formatName() const override { return QStringLiteral("Vector ASCII Log"); }

private:
    /// 解析单行 ASC 数据
    /// @return true 解析成功，frame 已填充
    static bool parseLine(const QString &line, CanFrame &frame);
};

#endif // ASC_IMPORTER_H
