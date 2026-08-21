#ifndef IPROTOCOLADAPTER_H
#define IPROTOCOLADAPTER_H

#include "core/busmessage.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

/**
 * @file iprotocoladapter.h
 * @brief 协议适配层接口（doc/flow.md §五；M2 预埋，纯新增不接线）
 *
 * ABI 契约复用驱动插件模式（同 Qt 6.8.x + MinGW-w64 + C++17；跨边界仅
 * Qt 值类型/POD；接口只增不改——新增虚函数一律追加末尾、带默认实现并升 IID，
 * 见风险对策 R7）。
 *
 * M2 落地范围（doc/flow.md §13.3）：身份 / 源能力 / 通道 / 解析器声明 /
 * 解码 / Trace 列 / 文件 IO；协议专属源配置页（createSourceConfigPage，
 * 依赖 FlowSession）推迟到 F1 剩余时按「末尾追加 + 默认实现」纪律补入。
 */

class IProtocolAdapter
{
public:
    virtual ~IProtocolAdapter() = default;

    // ---- 身份 ----
    virtual QString protocolId() const = 0;    ///< "can" / "ethercat" / "general" / 第三方
    virtual QString displayName() const = 0;   ///< "CAN Flow" / "EtherCAT Flow" / "通用 Flow"
    virtual QString iconPath() const = 0;      ///< :/icons/protocols/can.svg ...

    // ---- 源能力 ----
    virtual QStringList supportedSources() const = 0;   ///< {"hardware","file","simulator"} 子集

    // ---- 通道 ----
    virtual int maxChannels() const = 0;       ///< CAN=16 / EtherCAT=1(网口) / General=8

    // ---- 解析器（协议描述文件；角色定义详见 doc/flow.md §六） ----
    virtual QStringList acceptedParsers() const = 0;   ///< {"dbc","arxml","j1939dbc"} 等子集

    // ---- 解码（冷路径：详情/Graphic/导出用；信号定义来自流会话已加载的解析器） ----
    struct DecodedSignal {
        QString name;
        double value = 0.0;
        QString unit;
        QString raw;
    };
    virtual QList<DecodedSignal> decode(const BusMessage &msg) const = 0;

    // ---- Trace 展示 ----
    struct TraceColumnDef {
        QString key;           ///< 列键（协议内唯一，如 "id" / "dlc"）
        QString title;         ///< 显示标题
        int width = 0;         ///< 建议列宽（px；0 = 默认）
    };
    virtual QList<TraceColumnDef> traceColumns() const = 0;   ///< 协议专属列
    virtual QString formatField(const BusMessage &msg, const QString &key) const = 0;

    // ---- 文件 IO ----
    virtual QStringList fileFilters() const = 0;   ///< "*.blf *.asc" / "*.pcapng" / "*.bin *.csv"

    // ---- F1 剩余追加区（createSourceConfigPage 等：末尾追加 + 默认实现 + 升 IID） ----
};

Q_DECLARE_INTERFACE(IProtocolAdapter, "com.sin.openbus.IProtocolAdapter/1.0")

#endif // IPROTOCOLADAPTER_H
