#ifndef ARXML_IMPORTER_H
#define ARXML_IMPORTER_H

#include <QString>
#include "core/dbcdata.h"

/**
 * @brief ARXML (AUTOSAR XML) 导入器
 *
 * 解析 ARXML 文件，提取 CAN 通信描述（Frame、PDU、ISignal），
 * 转换为内部 DbcData 结构。
 *
 * 支持的 ARXML 元素：
 *   - I-SIGNAL: 信号定义 (name, length, bitPosition)
 *   - I-SIGNAL-I-PDU: PDU 组装 (signal-to-bit mapping)
 *   - CAN-CLUSTER: CAN 总线集群 (channel, baudrate)
 *   - CAN-FRAME: 帧定义 (id, length, pduRef)
 *   - SYSTEM-SIGNAL: 系统信号 (data type, min/max)
 */
class ArxmlImporter
{
public:
    ArxmlImporter();

    /// 导入 ARXML 文件，返回解析后的 DbcFile
    bool loadFromFile(const QString &filePath, DbcFile &outDbc);

    /// 最后一次错误信息
    QString lastError() const { return m_lastError; }

private:
    QString m_lastError;

    /// 解析 I-SIGNAL 元素
    void parseISignal(const void *node, DbcSignal &outSig);

    /// 解析 I-SIGNAL-I-PDU 元素（PDU 内信号布局）
    void parsePdu(const void *node, DbcMessage &outMsg);

    /// 解析 CAN-FRAME 元素
    void parseCanFrame(const void *node, DbcMessage &outMsg);

    /// 解析 CAN-CLUSTER 元素
    void parseCanCluster(const void *node);

    /// 查找短名引用（SHORT-NAME → 元素查找）
    const void *resolveRef(const QString &ref);
};

#endif // ARXML_IMPORTER_H
