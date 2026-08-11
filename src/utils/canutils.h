#ifndef CANUTILS_H
#define CANUTILS_H

#include <QString>
#include <QDateTime>
#include <functional>

struct CanFrame;

/**
 * @brief CAN 相关工具函数
 *
 * 包含格式化显示、过滤表达式解析等功能。
 */
namespace CanUtils
{
    // ---- 格式化 ----

    /// 时间戳格式化（秒 -> "0.000123"），默认 6 位小数（微秒精度）
    QString formatTime(double seconds);
    /// 时间戳格式化（带精度控制）
    /// precision: -1=自动, 0=秒, 3=毫秒, 6=微秒, 9=纳秒
    QString formatTime(double seconds, int precision);

    /// 日期+时间格式化（从捕获起始的 wall-clock + 偏移秒）
    /// 返回 "yyyy-MM-dd HH:mm:ss.zzzzzz" 格式
    QString formatDateTime(const QDateTime &start, double seconds, int precision);

    /// CAN ID 格式化（扩展帧 8 位 hex，标准帧 3 位 hex）
    QString formatId(quint32 id, bool extended);

    /// 数据字节格式化（"01 02 03 ..."）
    QString formatData(const QByteArray &data);

    /// DLC 显示（CAN FD 时显示实际长度）
    QString formatDlc(quint8 dlc, bool fd);

    /// 帧标志摘要（"FD BRS ESI" 等）
    QString formatFlags(const CanFrame &frame);

    // ---- 过滤器 ----

    /// 过滤谓词函数
    using FilterPredicate = std::function<bool(const CanFrame &)>;

    /// 解析过滤表达式，返回谓词；解析失败返回空 predicate
    /// 支持语法：
    ///   <hex>                按匹配 ID（如 "123", "0x123"）
    ///   id == <hex>          匹配指定 ID
    ///   id != <hex>          排除指定 ID
    ///   id in <hex>,<hex>    匹配多个 ID
    ///   dlc > <n>            DLC 大于 n
    ///   dlc == <n>           DLC 等于 n
    ///   dlc < <n>            DLC 小于 n
    ///   fd                   仅 CAN FD 帧
    ///   !fd                  仅经典 CAN 帧
    ///   ext                  仅扩展帧
    ///   std                  仅标准帧
    ///   rx                   仅接收帧
    ///   tx                   仅发送帧
    ///   data contains <hex>  数据包含指定字节序列
    ///   ch == <n>            匹配通道号
    ///   <expr> and <expr>    逻辑与
    ///   <expr> or <expr>     逻辑或
    ///   not <expr>           逻辑非
    FilterPredicate parseFilter(const QString &expr);

    /// 检查过滤表达式是否合法
    bool isFilterValid(const QString &expr);

    /// 返回过滤器语法帮助文本
    QString filterHelp();

    /// 从字符串解析十六进制数值
    quint32 parseHex(const QString &s);
}

#endif // CANUTILS_H
