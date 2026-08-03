#include "canfileio_factory.h"
#include "canfileio.h"
#include "blf.h"
#include "asc.h"
#include "csv.h"
#include "pcap_reader.h"
#include "trc_reader.h"

#include <QFileInfo>

std::unique_ptr<CanFileWriter> CanFileIOFactory::createWriter(CanFileIO::Format fmt)
{
    switch (fmt) {
    case CanFileIO::Format::BLF:
        return std::make_unique<BlfWriter>();
    case CanFileIO::Format::ASC:
        return std::make_unique<AscWriter>();
    case CanFileIO::Format::CSV:
        return std::make_unique<CsvWriter>();
    default:
        return nullptr;
    }
}

std::unique_ptr<CanFileWriter> CanFileIOFactory::createWriter(const QString &filePath)
{
    QString suffix = QFileInfo(filePath).suffix();
    return createWriter(CanFileIO::formatFromSuffix(suffix));
}

std::unique_ptr<CanFileReader> CanFileIOFactory::createReader(CanFileIO::Format fmt)
{
    switch (fmt) {
    case CanFileIO::Format::BLF:
        return std::make_unique<BlfReader>();
    case CanFileIO::Format::ASC:
        return std::make_unique<AscReader>();
    case CanFileIO::Format::CSV:
        return std::make_unique<CsvReader>();
    case CanFileIO::Format::PCAP:
        return std::make_unique<PcapReader>();
    case CanFileIO::Format::TRC:
        return std::make_unique<TrcReader>();
    default:
        return nullptr;
    }
}

std::unique_ptr<CanFileReader> CanFileIOFactory::createReader(const QString &filePath)
{
    QString suffix = QFileInfo(filePath).suffix();
    return createReader(CanFileIO::formatFromSuffix(suffix));
}

bool CanFileIOFactory::canWrite(const QString &suffix)
{
    CanFileIO::Format fmt = CanFileIO::formatFromSuffix(suffix);
    return fmt == CanFileIO::Format::BLF ||
           fmt == CanFileIO::Format::ASC ||
           fmt == CanFileIO::Format::CSV;
}

bool CanFileIOFactory::canRead(const QString &suffix)
{
    CanFileIO::Format fmt = CanFileIO::formatFromSuffix(suffix);
    return fmt != CanFileIO::Format::Unknown;
}
