#ifndef DBCDATA_H
#define DBCDATA_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QPair>
#include <QVariant>
#include <QMetaType>

/**
 * @brief DBC 值描述（值表条目）
 *
 * 对应 VAL_ / VAL_TABLE_ 中的 "value \"description\"" 对
 */
struct DbcValueDesc
{
    int value = 0;
    QString description;
};

/**
 * @brief DBC 命名值表
 *
 * 对应 VAL_TABLE_ 段，可被多个信号引用
 */
struct DbcValueTable
{
    QString name;
    QList<DbcValueDesc> entries;

    /// 根据原始值查找描述文本
    QString lookup(int value) const
    {
        for (const auto &e : entries)
            if (e.value == value) return e.description;
        return {};
    }
};

/**
 * @brief DBC 属性定义
 *
 * 对应 BA_DEF_ / BA_DEF_DEF_ 段
 */
struct DbcAttributeDef
{
    enum class DataType { Int, Float, String, Enum };
    enum class Scope { Network, Node, Message, Signal };

    QString name;
    DataType dataType = DataType::Int;
    Scope scope = Scope::Message;
    QVariant defaultValue;
    QVariant minValue;
    QVariant maxValue;
    QStringList enumChoices;  // 仅 Enum 类型
};

/**
 * @brief DBC 属性值（已设置的实例）
 */
struct DbcAttributeValue
{
    QString attributeName;
    // 目标标识：scope=Node 时为节点名, scope=Message 时为 canId, scope=Signal 时为 canId+signalName
    quint32 canId = 0;
    QString nodeName;
    QString signalName;
    QVariant value;
};

/**
 * @brief DBC 网络节点
 *
 * 对应 BU_ 段中的节点定义
 */
struct DbcNode
{
    QString name;
    QString comment;
    // 该节点发送的报文 ID 列表
    QList<quint32> txMessageIds;
    // 该节点接收的信号列表 (canId, signalName)
    QList<QPair<quint32, QString>> rxSignals;
};

/**
 * @brief DBC 信号定义
 */
struct DbcSignal
{
    QString name;
    int startBit = 0;
    int bitLength = 1;
    bool littleEndian = true;   // Intel = true, Motorola = false
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;
    QString receiver;
    QString comment;

    // 多路复用
    enum class MuxType { None, Multiplexor, Multiplexed };
    MuxType muxType = MuxType::None;
    int muxValue = -1;

    // 值表
    QString valueTableName;           // 引用的命名值表名
    QList<DbcValueDesc> valueTable;   // 内联值表 (VAL_ 段直接定义)

    // 从原始数据中解码信号值
    double decode(const QByteArray &data) const;

    /// 查找值描述
    QString lookupValueDesc(int rawValue) const
    {
        for (const auto &e : valueTable)
            if (e.value == rawValue) return e.description;
        return {};
    }
};

/**
 * @brief DBC 报文定义
 */
struct DbcMessage
{
    quint32 id = 0;
    QString name;
    int dlc = 8;
    QString sender;
    QList<DbcSignal> signalList;
    QString comment;

    // 扩展属性
    int cycleTime = 0;          // GenMsgCycleTime (ms)
    QString sendType;           // GenMsgSendType
    QList<QString> txNodes;     // 发送节点列表 (BO_TX_BU_)

    const DbcSignal *findSignal(const QString &name) const
    {
        for (const auto &s : signalList)
            if (s.name == name) return &s;
        return nullptr;
    }

    DbcSignal *findSignal(const QString &name)
    {
        for (auto &s : signalList)
            if (s.name == name) return &s;
        return nullptr;
    }
};

/**
 * @brief DBC 文件（一个 DBC 可包含多个报文）
 */
struct DbcFile
{
    QString filePath;
    QString fileName;
    QString version;
    QList<DbcMessage> messages;
    QList<DbcNode> nodes;
    QList<DbcValueTable> valueTables;
    QList<DbcAttributeDef> attributeDefs;
    QList<DbcAttributeValue> attributeValues;

    const DbcMessage *findMessage(quint32 id) const
    {
        for (const auto &m : messages)
            if (m.id == id) return &m;
        return nullptr;
    }

    DbcMessage *findMessage(quint32 id)
    {
        for (auto &m : messages)
            if (m.id == id) return &m;
        return nullptr;
    }

    const DbcNode *findNode(const QString &name) const
    {
        for (const auto &n : nodes)
            if (n.name == name) return &n;
        return nullptr;
    }

    const DbcValueTable *findValueTable(const QString &name) const
    {
        for (const auto &vt : valueTables)
            if (vt.name == name) return &vt;
        return nullptr;
    }

    /// 获取报文属性值
    QVariant getAttributeValue(const QString &attrName, quint32 canId) const
    {
        for (const auto &av : attributeValues)
            if (av.attributeName == attrName && av.canId == canId && av.signalName.isEmpty())
                return av.value;
        return {};
    }

    /// 获取信号属性值
    QVariant getSignalAttributeValue(const QString &attrName, quint32 canId, const QString &sigName) const
    {
        for (const auto &av : attributeValues)
            if (av.attributeName == attrName && av.canId == canId && av.signalName == sigName)
                return av.value;
        return {};
    }
};

#endif // DBCDATA_H
