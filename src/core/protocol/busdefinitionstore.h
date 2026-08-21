#ifndef BUSDEFINITIONSTORE_H
#define BUSDEFINITIONSTORE_H

#include "core/protocol/busdefinition.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

/**
 * @file busdefinitionstore.h
 * @brief 统一定义存储（doc/flow.md §6.3；M2 预埋，纯新增不接线）
 *
 * 持有全部已加载 BusDefinitionSet，提供跨文件 O(1) 索引查找——
 * 接替 DbcManager 的跨文件索引职责（多解析器命名空间按
 * 「流会话 + parserId + 文件」隔离，避免不同解析器的同名报文互相覆盖）。
 * M2 仅落地存储与索引；流会话维度隔离随 F1 剩余（FlowSession）补入。
 */
class BusDefinitionStore : public QObject
{
    Q_OBJECT

public:
    static BusDefinitionStore *instance();

    /// 加入一个解析结果集（同 filePath 重复加入 = 替换）
    void addDefinitionSet(const BusDefinitionSet &set);

    /// 移除指定文件的定义集（不存在返回 false）
    bool removeDefinitionSet(const QString &filePath);

    /// 全部定义集（加载顺序）
    const QList<BusDefinitionSet> &definitionSets() const { return m_sets; }

    /// 按 protocolId + 报文 id 跨文件查找（同 id 多文件时取首个加载者）
    const BusMessageDef *findMessage(const QString &protocolId, quint32 id) const;

    /// 按 protocolId + 报文 id + 信号名跨文件查找
    const BusSignalDef *findSignal(const QString &protocolId, quint32 id,
                                   const QString &signalName) const;

    /// 指定协议的报文总数
    int messageCount(const QString &protocolId) const;

signals:
    void definitionSetAdded(const QString &filePath, const QString &parserId);
    void definitionSetRemoved(const QString &filePath);

private:
    explicit BusDefinitionStore(QObject *parent = nullptr);

    /// 协议内索引：protocolId → (报文 id → 所在定义集内偏移)
    QHash<QString, QHash<quint32, int>> m_index;

    void rebuildIndex();

    QList<BusDefinitionSet> m_sets;
};

#endif // BUSDEFINITIONSTORE_H
