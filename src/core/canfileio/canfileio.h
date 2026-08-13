#ifndef CANFILEIO_H
#define CANFILEIO_H

#include <QString>
#include <QVector>

struct CanFrame;

/**
 * @brief 报文文件格式 I/O 抽象层
 *
 * 支持的主流 CAN/CAN FD 报文文件格式：
 * - BLF (Binary Logging Format)  — Vector 二进制格式，汽车行业最常用
 * - ASC (ASCII Logging Format)    — Vector 文本格式，人可读
 * - CSV (Comma-Separated Values)  — 通用文本格式
 * - PCAP (libpcap Network Capture) — 标准网络捕获格式，Linux SocketCAN/Wireshark
 * - TRC (Trace Format)            — Vector 旧版文本格式
 *
 * 已移除 .openbus 专有格式支持，全面转向行业主流格式。
 */

namespace CanFileIO {

/// 支持的报文文件格式
enum class Format {
    BLF,   ///< Vector Binary Logging Format (.blf)
    ASC,   ///< Vector ASCII Logging Format (.asc)
    CSV,   ///< Comma-Separated Values (.csv)
    PCAP,  ///< libpcap Network Capture (.pcap / .pcapng)
    TRC,   ///< Vector Trace Format (.trc)
    Unknown
};

/// 根据文件扩展名推断格式
Format formatFromSuffix(const QString &suffix);

/// 获取格式的扩展名（不含点）
QString suffix(Format fmt);

/// 获取格式的显示名称
QString displayName(Format fmt);

/// 获取格式的文件过滤器字符串（如 "BLF 文件 (*.blf)"）
QString fileFilter(Format fmt);

/// 获取所有支持格式的文件过滤器（用于 QFileDialog）
/// @param includeAll 是否包含“所有文件 (*.*)”
QString allFileFilters(bool includeAll = true);

/// 获取可写入格式的文件过滤器（BLF 写入暂未实现，不包含）
QString writableFileFilters();

} // namespace CanFileIO

// ============================================================
//  写入器接口 — 用于 Recorder 流式写入
// ============================================================

/**
 * @brief 报文文件写入器接口
 *
 * 子类实现具体格式（BLF/ASC/CSV 等）的写入逻辑。
 * 生命周期：open() → writeFrame() × N → close()
 */
class CanFileWriter
{
public:
    virtual ~CanFileWriter() = default;

    /// 打开文件准备写入
    virtual bool open(const QString &filePath) = 0;

    /// 写入单帧（流式录制）
    virtual void writeFrame(const CanFrame &frame) = 0;

    /// 关闭文件，完成收尾（如回写帧数、文件头）
    virtual void close() = 0;

    /// 是否已打开
    virtual bool isOpen() const = 0;

    /// 已写入的帧数
    virtual int frameCount() const = 0;
};

// ============================================================
//  读取器接口 — 用于 Player 批量加载
// ============================================================

/**
 * @brief 报文文件读取器接口
 *
 * 子类实现具体格式的读取逻辑。
 * 生命周期：open() → readAll() → close()
 */
class CanFileReader
{
public:
    virtual ~CanFileReader() = default;

    /// 打开文件准备读取
    virtual bool open(const QString &filePath) = 0;

    /// 读取所有帧到 frames 容器
    /// @return 成功读取的帧数，-1 表示失败
    virtual int readAll(QVector<CanFrame> &frames) = 0;

    /// 关闭文件
    virtual void close() = 0;

    /// 是否已打开
    virtual bool isOpen() const = 0;
};

#endif // CANFILEIO_H
