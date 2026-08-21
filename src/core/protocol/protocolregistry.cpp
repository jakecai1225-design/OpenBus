#include "protocolregistry.h"
#include "canprotocoladapter.h"

#include <QPointer>

ProtocolRegistry *ProtocolRegistry::instance()
{
    static QPointer<ProtocolRegistry> inst;
    if (!inst) {
        inst = new ProtocolRegistry();
        ensureBuiltinRegistered();
    }
    return inst;
}

ProtocolRegistry::ProtocolRegistry(QObject *parent)
    : QObject(parent)
{
}

void ProtocolRegistry::ensureBuiltinRegistered()
{
    // 内置适配器懒注册：首个访问点即完成（M2 仅 CAN，§13.3）
    instance()->registerAdapter(new CanProtocolAdapter(instance()));
}

bool ProtocolRegistry::registerAdapter(IProtocolAdapter *adapter)
{
    if (!adapter)
        return false;
    if (findAdapter(adapter->protocolId()))
        return false;   // 重复 protocolId：忽略（多源注册冲突防护）
    m_adapters.append(adapter);
    emit adapterRegistered(adapter->protocolId());
    return true;
}

IProtocolAdapter *ProtocolRegistry::findAdapter(const QString &protocolId) const
{
    for (auto *a : m_adapters)
        if (a->protocolId() == protocolId)
            return a;
    return nullptr;
}
