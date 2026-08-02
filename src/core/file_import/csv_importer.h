#ifndef CSV_IMPORTER_H
#define CSV_IMPORTER_H

#include "core/file_import/file_importer.h"

/**
 * @brief CSV 文件导入器
 *
 * 解析通用 CSV 格式日志文件。自动识别表头列名，
 * 支持以下列名（不区分大小写）：
 *   - Time / Timestamp / 时间
 *   - Channel / 通道
 *   - ID / CanId
 *   - Dir / Direction / 方向
 *   - Extended / Ext
 *   - FD / CanFD
 *   - DLC / Length
 *   - Data / Payload / 数据
 *
 * 无表头时按固定顺序解析：Time,Channel,ID,Dir,DLC,Data
 */
class CsvImporter : public FileImporter
{
public:
    QVector<CanFrame> importFile(
        const QString &filePath,
        std::function<void(double)> progress = nullptr) override;

    QStringList supportedExtensions() const override { return {"csv"}; }
    QString formatName() const override { return QStringLiteral("CSV Log"); }

private:
    /// 列索引映射
    struct ColumnMap
    {
        int time = -1;
        int channel = -1;
        int id = -1;
        int dir = -1;
        int extended = -1;
        int fd = -1;
        int dlc = -1;
        int data = -1;
    };

    /// 从表头行解析列映射
    static ColumnMap parseHeader(const QString &headerLine);

    /// 从 CSV 字段构建 CanFrame
    static bool buildFrame(const QStringList &fields, const ColumnMap &map, CanFrame &frame);
};

#endif // CSV_IMPORTER_H
