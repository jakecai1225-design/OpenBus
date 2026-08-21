#ifndef CANPROTOCOLADAPTER_H
#define CANPROTOCOLADAPTER_H

#include "core/protocol/iprotocoladapter.h"

class DbcManager;

/**
 * @file canprotocoladapter.h
 * @brief 内置 CAN 协议适配器（doc/flow.md §5.2；M2 薄实现，纯新增不接线）
 *
 * 零重构包装：源能力 = CanDeviceManager（硬件）+ Player（文件）+
 * CanSimulator（仿真）；解析器 = DBC（经 DbcManager 兼容壳）；
 * traceColumns() 输出现有 CanTraceModel 12 列定义；decode() 复用
 * DbcManager 解码路径。热路径不经过本适配器（M2 预埋，§13.5）。
 */
class CanProtocolAdapter : public QObject, public IProtocolAdapter
{
    Q_OBJECT
    Q_INTERFACES(IProtocolAdapter)

public:
    explicit CanProtocolAdapter(QObject *parent = nullptr);

    // ---- IProtocolAdapter ----
    QString protocolId() const override;
    QString displayName() const override;
    QString iconPath() const override;
    QStringList supportedSources() const override;
    int maxChannels() const override;
    QStringList acceptedParsers() const override;
    QList<DecodedSignal> decode(const BusMessage &msg) const override;
    QList<TraceColumnDef> traceColumns() const override;
    QString formatField(const BusMessage &msg, const QString &key) const override;
    QStringList fileFilters() const override;

    // ---- 会话接线（M2 由单测注入；F1 剩余时由壳接线） ----

    /// 设置 DBC 管理器引用（decode 的信号定义来源；空 = 解码返回空表）
    void setDbcManager(DbcManager *mgr) { m_dbcManager = mgr; }

private:
    DbcManager *m_dbcManager = nullptr;
};

#endif // CANPROTOCOLADAPTER_H
