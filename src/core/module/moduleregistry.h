#ifndef OPENBUS_CORE_MODULE_MODULEREGISTRY_H
#define OPENBUS_CORE_MODULE_MODULEREGISTRY_H

#include <functional>

#include <QHash>
#include <QString>
#include <QStringList>

#include "core/module/imodule.h"

/**
 * @file moduleregistry.h
 * @brief 业务模块注册表（doc/拆分应用实施方案.md §4.2）
 *
 * 壳在 main() 里显式注册各模块工厂，业务代码经此取模块实例，
 * 禁止直接 include 其他业务模块的头文件（壳 ↔ 业务、业务 ↔ 业务
 * 的全部装配关系都收敛到注册表）。
 *
 * 工厂来源：
 *  - B0（单体链接验证期）：注册静态链接的工厂 lambda
 *  - B1 起（DLL 化）：注册各 DLL 的 openbus_create<Xxx>Module C 工厂
 *    （按模块唯一命名，避免多 DLL 链接期同名冲突）
 *
 * 仅主线程使用（main() 注册 + UI 线程取用），不做加锁。
 */
class ModuleRegistry {
public:
    using ModuleFactory = std::function<IBusinessModule *()>;

    static ModuleRegistry *instance();

    /// 注册模块工厂（重复 id 时后者覆盖前者）
    void registerModule(const QString &id, ModuleFactory factory);

    /// 取模块实例（首次访问时惰性创建并缓存；未注册返回 nullptr）
    IBusinessModule *module(const QString &id) const;

    /// 已注册的全部模块 id（注册顺序不保证，仅供枚举）
    QStringList ids() const;

private:
    ModuleRegistry() = default;

    struct Entry {
        ModuleFactory factory;
        IBusinessModule *instance = nullptr;
    };
    mutable QHash<QString, Entry> m_entries;   ///< module() 惰性创建需要 mutable
};

#endif // OPENBUS_CORE_MODULE_MODULEREGISTRY_H
