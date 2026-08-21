#ifndef PROTOCOLREGISTRY_H
#define PROTOCOLREGISTRY_H

#include "iprotocoladapter.h"

#include <QList>
#include <QObject>
#include <QString>

/**
 * @file protocolregistry.h
 * @brief 协议适配器注册表（doc/flow.md §五；M2 预埋，纯新增不接线）
 *
 * 全进程唯一（openbus_data 单例，§4.2 决策 2），枚举内置与第三方
 * （.oflow）协议适配器。M2 仅注册内置 CAN；F4 协议包经
 * QPluginLoader 追加注册。接口一经注册 ABI 冻结（只增不改，R7）。
 */
class ProtocolRegistry : public QObject
{
    Q_OBJECT

public:
    static ProtocolRegistry *instance();

    /// 注册适配器（所有权归注册表；重复 protocolId 忽略并返回 false）
    bool registerAdapter(IProtocolAdapter *adapter);

    /// 全部已注册适配器（注册顺序）
    const QList<IProtocolAdapter *> &adapters() const { return m_adapters; }

    /// 按 protocolId 查找（未注册返回 nullptr）
    IProtocolAdapter *findAdapter(const QString &protocolId) const;

    /// 已注册协议数量（侧栏折叠节生成依据）
    int count() const { return m_adapters.size(); }

signals:
    void adapterRegistered(const QString &protocolId);

private:
    explicit ProtocolRegistry(QObject *parent = nullptr);
    static void ensureBuiltinRegistered();

    QList<IProtocolAdapter *> m_adapters;
};

#endif // PROTOCOLREGISTRY_H
