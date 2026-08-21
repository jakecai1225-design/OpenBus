#ifndef DBCPARSER_H
#define DBCPARSER_H

#include "core/protocol/ibusparser.h"

/**
 * @file dbcparser.h
 * @brief DBC 解析器直通实现（doc/flow.md §6.4；M2 预埋，纯新增不接线）
 *
 * 内部经 DbcManager 既有解析器产出 BusDefinitionSet（验证「解析器 →
 * 统一定义模型」映射模式）；不改变 DbcManager 现有 API 与行为——
 * 双入口并存（直连 loadDbc / 注册表管道），DbcManager 冻结（§6.4）。
 */
class DbcParser : public QObject, public IBusParser
{
    Q_OBJECT
    Q_INTERFACES(IBusParser)

public:
    explicit DbcParser(QObject *parent = nullptr);

    // ---- IBusParser ----
    QString parserId() const override;
    QString displayName() const override;
    QString iconPath() const override;
    QStringList fileExtensions() const override;
    BusDefinitionSet parse(const QString &filePath, QString *error) const override;
};

#endif // DBCPARSER_H
