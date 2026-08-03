#ifndef PCAP_READER_H
#define PCAP_READER_H

#include "canfileio.h"
#include <QFile>

/// PCAP (libpcap Network Capture) 读取器
///
/// 支持读取 Linux SocketCAN 抓包文件（Wireshark / can-utils / tcpdump）
/// 支持 microsecond 和 nanosecond 时间精度
/// 支持经典 CAN (LINKTYPE_CAN=227) 和 CAN FD 帧
class PcapReader : public CanFileReader
{
public:
    PcapReader();
    ~PcapReader() override;

    bool open(const QString &filePath) override;
    int readAll(QVector<CanFrame> &frames) override;
    void close() override;
    bool isOpen() const override { return m_file.isOpen(); }

private:
    QFile m_file;
    bool m_nanosecond = false;  // 纳秒精度标志
    quint32 m_linkType = 0;    // 链路类型
};

#endif // PCAP_READER_H
