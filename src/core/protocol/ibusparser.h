#ifndef IBUSPARSER_H
#define IBUSPARSER_H

#include "core/protocol/busdefinition.h"

#include <QString>
#include <QStringList>

/**
 * @file ibusparser.h
 * @brief 解析器角色接口（doc/flow.md §六；M2 预埋，纯新增不接线）
 *
 * 解析器只负责把协议描述文件变成统一定义（BusDefinitionSet），
 * 不关心数据从哪条流来；流类型（适配器）通过 acceptedParsers()
 * 声明接受哪些解析器。ABI 纪律同 IProtocolAdapter（只增不改，R7）。
 */
class IBusParser
{
public:
    virtual ~IBusParser() = default;

    virtual QString parserId() const = 0;      ///< "dbc" / "arxml" / "j1939dbc" / "eni" / "esi" / "layout"
    virtual QString displayName() const = 0;   ///< "DBC 数据库" / "ARXML" / "ENI (EtherCAT)" ...
    virtual QString iconPath() const = 0;
    virtual QStringList fileExtensions() const = 0;   ///< {"dbc"} / {"arxml"} / {"eni","xml"} ...

    /// 解析描述文件 → 统一定义集（失败返回空集并经 error 上报）
    virtual BusDefinitionSet parse(const QString &filePath, QString *error) const = 0;
};

Q_DECLARE_INTERFACE(IBusParser, "com.sin.openbus.IBusParser/1.0")

#endif // IBUSPARSER_H
