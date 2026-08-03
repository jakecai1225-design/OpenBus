#include "blf_importer.h"

#include "core/canfileio/blf.h"

#include <QDebug>

// ============================================================
//  BlfImporter::importFile — 委托 canfileio BlfReader (vector_blf)
// ============================================================

QVector<CanFrame> BlfImporter::importFile(
    const QString &filePath,
    std::function<void(double)> progress)
{
    QVector<CanFrame> result;

    BlfReader reader;
    if (!reader.open(filePath)) {
        qWarning() << "BLF: 无法打开文件" << filePath;
        return result;
    }

    int count = reader.readAll(result);
    reader.close();

    if (count < 0) {
        qWarning() << "BLF: 解析失败" << filePath;
        result.clear();
        return result;
    }

    if (progress) progress(1.0);

    qDebug() << "BLF 导入完成:" << result.size() << "帧";
    return result;
}
