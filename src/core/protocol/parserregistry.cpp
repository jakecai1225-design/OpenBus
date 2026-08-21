#include "parserregistry.h"
#include "dbcparser.h"

#include <QPointer>

ParserRegistry *ParserRegistry::instance()
{
    static QPointer<ParserRegistry> inst;
    if (!inst) {
        inst = new ParserRegistry();
        ensureBuiltinRegistered();
    }
    return inst;
}

ParserRegistry::ParserRegistry(QObject *parent)
    : QObject(parent)
{
}

void ParserRegistry::ensureBuiltinRegistered()
{
    // 内置解析器懒注册：首个访问点即完成（M2 仅 DBC 直通，§13.3）
    instance()->registerParser(new DbcParser(instance()));
}

bool ParserRegistry::registerParser(IBusParser *parser)
{
    if (!parser)
        return false;
    if (findParser(parser->parserId()))
        return false;   // 重复 parserId：忽略
    m_parsers.append(parser);
    emit parserRegistered(parser->parserId());
    return true;
}

IBusParser *ParserRegistry::findParser(const QString &parserId) const
{
    for (auto *p : m_parsers)
        if (p->parserId() == parserId)
            return p;
    return nullptr;
}

IBusParser *ParserRegistry::findParserForExtension(const QString &ext) const
{
    const QString e = ext.startsWith(QLatin1Char('.')) ? ext.mid(1) : ext;
    for (auto *p : m_parsers)
        if (p->fileExtensions().contains(e, Qt::CaseInsensitive))
            return p;
    return nullptr;
}
