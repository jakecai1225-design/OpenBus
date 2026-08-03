#include "core/file_import/file_importer.h"
#include "core/file_import/asc_importer.h"
#include "core/file_import/csv_importer.h"
#include "core/file_import/blf_importer.h"

#include <QFileInfo>

std::unique_ptr<FileImporter> FileImportFactory::create(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == QLatin1String("asc"))
        return std::make_unique<AscImporter>();
    if (suffix == QLatin1String("csv"))
        return std::make_unique<CsvImporter>();
    if (suffix == QLatin1String("blf"))
        return std::make_unique<BlfImporter>();

    return nullptr;
}

QStringList FileImportFactory::fileFilters()
{
    return {
        QStringLiteral("日志文件 (*.asc *.csv *.blf)"),
        QStringLiteral("Vector ASC (*.asc)"),
        QStringLiteral("CSV (*.csv)"),
        QStringLiteral("Vector BLF (*.blf)"),
        QStringLiteral("所有文件 (*.*)"),
    };
}
