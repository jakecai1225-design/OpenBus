#ifndef BUSDEFINITION_H
#define BUSDEFINITION_H

#include <QList>
#include <QString>
#include <cstdint>

/**
 * @file busdefinition.h
 * @brief 统一报文/信号定义模型（doc/flow.md §6.2；M2 预埋，纯新增不接线）
 *
 * 解析器的输出不是各协议私有结构，而是统一「报文/信号定义」模型——
 * DBC / ARXML / J1939 / ENI / ESI / 字段布局 JSON 均映射到本模型。
 * 演化纪律（R8）：公共字段只增不改；无法映射的协议语义留在解析器
 * 内部消化（适配器 decode() 自解释），不污染统一模型。
 */

/**
 * @brief 统一信号定义
 *
 * 各协议映射：DBC signal / ARXML ISignal / J1939 SPN / ENI PDO 映射对象 /
 * 字段布局 JSON 字段。
 */
struct BusSignalDef
{
    QString name;              ///< 信号名（DBC 信号 / PDO 映射对象 / 布局字段名）
    int startBit = 0;          ///< 起始位（相对所在报文 / 过程映像区）
    int bitLength = 0;
    bool littleEndian = true;
    double factor = 1.0;       ///< 缩放
    double offset = 0.0;       ///< 偏移
    QString unit;              ///< 单位
    QString comment;           ///< 注释 / SPN 描述
    // 值表（物理值 ↔ 含义）以 QVariantList 序列化追加（只增不改）
};

/**
 * @brief 统一报文定义
 *
 * 各协议映射：DBC message / ARXML ISignalIPdu / J1939 PGN / ENI 过程映像区 /
 * 布局帧。
 */
struct BusMessageDef
{
    quint32 id = 0;            ///< 协议内报文标识（CAN ID / PDO 基址 / 流帧编号）
    QString name;              ///< 报文名
    int length = 0;            ///< 长度（字节数）
    // 注意：命名沿用 DbcMessage::signalList——「signals」是 Qt 宏（Q_OBJECT
    // 信号段），作成员名会与 moc 预处理冲突（§6.2 文档代码块同步修正）
    QList<BusSignalDef> signalList;
};

/**
 * @brief 一个描述文件的解析结果
 */
struct BusDefinitionSet
{
    QString parserId;          ///< "dbc" / "arxml" / "j1939dbc" / "eni" / "esi" / "layout"
    QString filePath;
    QString protocolHint;      ///< 目标流类型提示（可空；加载时校验与流类型兼容）
    QList<BusMessageDef> messages;

    bool isEmpty() const { return messages.isEmpty(); }

    /// 按 id 查找报文定义（本文件内线性查找；跨文件索引走 BusDefinitionStore）
    const BusMessageDef *findMessage(quint32 id) const
    {
        for (const auto &m : messages)
            if (m.id == id)
                return &m;
        return nullptr;
    }
};

#endif // BUSDEFINITION_H
