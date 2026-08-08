#ifndef ARXML_EXPORTER_H
#define ARXML_EXPORTER_H

#include <QString>
#include "core/dbcdata.h"

/**
 * @brief ARXML (AUTOSAR XML) 导出器
 *
 * 将内部 DbcFile 结构导出为 ARXML 格式文件。
 * 生成的基本结构：
 *   <AUTOSAR>
 *     <AR-PACKAGES>
 *       <AR-PACKAGE>
 *         <SHORT-NAME>sin_export</SHORT-NAME>
 *         <ELEMENTS>
 *           <CAN-CLUSTER>...
 *           <CAN-FRAME>...
 *           <I-SIGNAL>...
 *           <I-SIGNAL-I-PDU>...
 *         </ELEMENTS>
 *       </AR-PACKAGE>
 *     </AR-PACKAGES>
 *   </AUTOSAR>
 */
class ArxmlExporter
{
public:
    ArxmlExporter();

    bool saveToFile(const QString &filePath, const DbcFile &dbc);

    QString lastError() const { return m_lastError; }

private:
    QString m_lastError;
};

#endif // ARXML_EXPORTER_H
