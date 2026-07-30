#include "canutils.h"
#include "core/canframe.h"
#include "core/filter_engine.h"

#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <memory>

namespace CanUtils {

// ============================================================
//  格式化
// ============================================================

QString formatTime(double seconds)
{
    return QString::number(seconds, 'f', 6);
}

QString formatId(quint32 id, bool extended)
{
    if (extended)
        return QStringLiteral("0x%1").arg(id & 0x1FFFFFFF, 8, 16, QChar('0')).toUpper();
    return QStringLiteral("0x%1").arg(id & 0x7FF, 3, 16, QChar('0')).toUpper();
}

QString formatData(const QByteArray &data)
{
    QString result;
    result.reserve(data.size() * 3);
    for (int i = 0; i < data.size(); ++i) {
        if (i > 0) result += ' ';
        result += QStringLiteral("%1").arg(static_cast<unsigned char>(data[i]), 2, 16, QChar('0')).toUpper();
    }
    return result;
}

QString formatDlc(quint8 dlc, bool fd)
{
    if (!fd)
        return QString::number(dlc);
    return QStringLiteral("%1 (%2)")
        .arg(dlc)
        .arg(CanFrame::dlcToLength(dlc));
}

QString formatFlags(const CanFrame &frame)
{
    QStringList flags;
    if (frame.fd)            flags << "FD";
    if (frame.bitrateSwitch) flags << "BRS";
    if (frame.errorState)    flags << "ESI";
    if (frame.extended)      flags << "EXT";
    if (frame.isErrorFrame()) flags << "ERR";
    return flags.join(' ');
}

// ============================================================
//  十六进制解析
// ============================================================

quint32 parseHex(const QString &s)
{
    QString trimmed = s.trimmed();
    if (trimmed.startsWith("0x", Qt::CaseInsensitive))
        trimmed = trimmed.mid(2);
    bool ok = false;
    quint32 val = trimmed.toUInt(&ok, 16);
    return ok ? val : 0xFFFFFFFF;
}

// ============================================================
//  过滤器接口（基于 exprtk 引擎）
// ============================================================

FilterPredicate parseFilter(const QString &expr)
{
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty())
        return [](const CanFrame &) { return true; };

    auto engine = std::make_shared<FilterEngine>();
    if (!engine->compile(trimmed))
        return {};
    return [engine](const CanFrame &f) { return engine->evaluate(f); };
}

bool isFilterValid(const QString &expr)
{
    QString trimmed = expr.trimmed();
    if (trimmed.isEmpty())
        return true;
    FilterEngine engine;
    return engine.compile(trimmed);
}

QString filterHelp()
{
    return QStringLiteral(
        "过滤器语法 (exprtk 引擎):\n"
        "\n"
        "  变量:\n"
        "    id    CAN ID (整数)\n"
        "    dlc   数据长度码\n"
        "    ch    通道号\n"
        "    time  时间戳 (秒)\n"
        "    fd    CAN FD 标志 (1/0)\n"
        "    ext   扩展帧标志 (1/0)\n"
        "    std   标准帧标志 (= !ext)\n"
        "    rx    接收方向 (1/0)\n"
        "    tx    发送方向 (= !rx)\n"
        "\n"
        "  运算符:\n"
        "    ==  !=  >  <  >=  <=  比较\n"
        "    and / &&    逻辑与\n"
        "    or  / ||    逻辑或\n"
        "    not / !     逻辑非\n"
        "\n"
        "  语法糖:\n"
        "    0x123                  等价于 id == 0x123\n"
        "    id in 0x100,0x200      匹配多个 ID\n"
        "    data contains 01 02    数据包含字节序列\n"
        "\n"
        "  示例:\n"
        "    id == 0x123 and dlc > 8\n"
        "    fd and ext\n"
        "    0x100 or 0x200\n"
        "    not (id == 0x100 or id == 0x200)\n"
        "    data contains 01 02 and ch == 1\n"
        "    dlc >= 8 and (fd or ext)\n"
        "    time > 1.5 and id != 0x7DF"
    );
}

} // namespace CanUtils
