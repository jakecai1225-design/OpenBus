#include "core/module/moduleregistry.h"

ModuleRegistry *ModuleRegistry::instance()
{
    static ModuleRegistry s_instance;
    return &s_instance;
}

void ModuleRegistry::registerModule(const QString &id, ModuleFactory factory)
{
    m_entries.insert(id, Entry{std::move(factory), nullptr});
}

IBusinessModule *ModuleRegistry::module(const QString &id) const
{
    auto it = m_entries.find(id);
    if (it == m_entries.end())
        return nullptr;
    if (!it->instance)
        it->instance = it->factory();
    return it->instance;
}

QStringList ModuleRegistry::ids() const
{
    QStringList list;
    list.reserve(m_entries.size());
    for (auto it = m_entries.keyBegin(); it != m_entries.keyEnd(); ++it)
        list << *it;
    return list;
}
