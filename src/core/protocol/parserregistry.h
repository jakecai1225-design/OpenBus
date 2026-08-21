#ifndef PARSERREGISTRY_H
#define PARSERREGISTRY_H

#include "ibusparser.h"

#include <QList>
#include <QObject>
#include <QString>

/**
 * @file parserregistry.h
 * @brief 解析器注册表（doc/flow.md §6.3；M2 预埋，纯新增不接线）
 *
 * 与 ProtocolRegistry 并列（openbus_data 单例）；.oflow 协议包可同时
 * 注册适配器与解析器（F4）。M2 仅注册内置 DBC 直通解析器。
 */
class ParserRegistry : public QObject
{
    Q_OBJECT

public:
    static ParserRegistry *instance();

    /// 注册解析器（所有权归注册表；重复 parserId 忽略并返回 false）
    bool registerParser(IBusParser *parser);

    /// 全部已注册解析器（注册顺序）
    const QList<IBusParser *> &parsers() const { return m_parsers; }

    /// 按 parserId 查找（未注册返回 nullptr）
    IBusParser *findParser(const QString &parserId) const;

    /// 按文件扩展名查找（不含点、大小写不敏感；多解析器声明同扩展时取首个）
    IBusParser *findParserForExtension(const QString &ext) const;

    /// 已注册解析器数量
    int count() const { return m_parsers.size(); }

signals:
    void parserRegistered(const QString &parserId);

private:
    explicit ParserRegistry(QObject *parent = nullptr);
    static void ensureBuiltinRegistered();

    QList<IBusParser *> m_parsers;
};

#endif // PARSERREGISTRY_H
