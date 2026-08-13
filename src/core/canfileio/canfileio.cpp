#include "canfileio.h"

namespace CanFileIO {

Format formatFromSuffix(const QString &suffix)
{
    QString s = suffix.toLower().trimmed();
    if (s.startsWith('.'))
        s = s.mid(1);

    if (s == "blf")  return Format::BLF;
    if (s == "asc")  return Format::ASC;
    if (s == "csv")  return Format::CSV;
    if (s == "pcap" || s == "pcapng") return Format::PCAP;
    if (s == "trc")  return Format::TRC;
    return Format::Unknown;
}

QString suffix(Format fmt)
{
    switch (fmt) {
    case Format::BLF:  return "blf";
    case Format::ASC:  return "asc";
    case Format::CSV:  return "csv";
    case Format::PCAP: return "pcap";
    case Format::TRC:  return "trc";
    default:           return {};
    }
}

QString displayName(Format fmt)
{
    switch (fmt) {
    case Format::BLF:  return "BLF";
    case Format::ASC:  return "ASC";
    case Format::CSV:  return "CSV";
    case Format::PCAP: return "PCAP";
    case Format::TRC:  return "TRC";
    default:           return {};
    }
}

QString fileFilter(Format fmt)
{
    switch (fmt) {
    case Format::BLF:  return "BLF 文件 (*.blf)";
    case Format::ASC:  return "ASC 文件 (*.asc)";
    case Format::CSV:  return "CSV 文件 (*.csv)";
    case Format::PCAP: return "PCAP 文件 (*.pcap *.pcapng)";
    case Format::TRC:  return "TRC 文件 (*.trc)";
    default:           return {};
    }
}

QString allFileFilters(bool includeAll)
{
    QString filter =
        "BLF 文件 (*.blf);;"
        "ASC 文件 (*.asc);;"
        "CSV 文件 (*.csv);;"
        "PCAP 文件 (*.pcap *.pcapng);;"
        "TRC 文件 (*.trc)";
    if (includeAll)
        filter += ";;所有文件 (*.*)";
    return filter;
}

QString writableFileFilters()
{
    return "BLF 文件 (*.blf);;ASC 文件 (*.asc);;CSV 文件 (*.csv)";
}

} // namespace CanFileIO
