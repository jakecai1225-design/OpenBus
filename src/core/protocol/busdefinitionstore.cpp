#include "busdefinitionstore.h"

#include <QPointer>

BusDefinitionStore *BusDefinitionStore::instance()
{
    static QPointer<BusDefinitionStore> inst;
    if (!inst)
        inst = new BusDefinitionStore();
    return inst;
}

BusDefinitionStore::BusDefinitionStore(QObject *parent)
    : QObject(parent)
{
}

void BusDefinitionStore::addDefinitionSet(const BusDefinitionSet &set)
{
    // 同文件重复加载 = 替换（与 DbcManager::loadDbc 的重复文件语义一致）
    removeDefinitionSet(set.filePath);
    m_sets.append(set);
    rebuildIndex();
    emit definitionSetAdded(set.filePath, set.parserId);
}

bool BusDefinitionStore::removeDefinitionSet(const QString &filePath)
{
    for (int i = 0; i < m_sets.size(); ++i) {
        if (m_sets[i].filePath == filePath) {
            m_sets.removeAt(i);
            rebuildIndex();
            emit definitionSetRemoved(filePath);
            return true;
        }
    }
    return false;
}

const BusMessageDef *BusDefinitionStore::findMessage(const QString &protocolId,
                                                     quint32 id) const
{
    // protocolHint 即目标流类型提示（M2 单协议直通场景 = 协议命名空间）
    for (const auto &s : m_sets) {
        if (s.protocolHint != protocolId)
            continue;
        if (const BusMessageDef *m = s.findMessage(id))
            return m;
    }
    return nullptr;
}

const BusSignalDef *BusDefinitionStore::findSignal(const QString &protocolId, quint32 id,
                                                   const QString &signalName) const
{
    for (const auto &s : m_sets) {
        if (s.protocolHint != protocolId)
            continue;
        if (const BusMessageDef *m = s.findMessage(id)) {
            for (const auto &sig : m->signalList)
                if (sig.name == signalName)
                    return &sig;
        }
    }
    return nullptr;
}

int BusDefinitionStore::messageCount(const QString &protocolId) const
{
    int n = 0;
    for (const auto &s : m_sets)
        if (s.protocolHint == protocolId)
            n += s.messages.size();
    return n;
}

void BusDefinitionStore::rebuildIndex()
{
    // M2 规模小（描述文件个位数）：线性查找即可；哈希索引结构先就位，
    // F1 剩余接入流会话命名空间时再启用
    m_index.clear();
    for (int i = 0; i < m_sets.size(); ++i) {
        const QString &hint = m_sets[i].protocolHint;
        for (int j = 0; j < m_sets[i].messages.size(); ++j) {
            const quint32 id = m_sets[i].messages[j].id;
            if (!m_index[hint].contains(id))
                m_index[hint].insert(id, i);
        }
    }
}
