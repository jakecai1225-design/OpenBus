#include "arxml_importer.h"
#include <QFile>
#include <QFileInfo>

// pugixml for XML parsing
#include "pugixml.hpp"

ArxmlImporter::ArxmlImporter()
{
}

bool ArxmlImporter::loadFromFile(const QString &filePath, DbcFile &outDbc)
{
    pugi::xml_document doc;
    if (!doc.load_file(filePath.toUtf8().constData())) {
        m_lastError = "Failed to load ARXML file: " + filePath;
        return false;
    }

    outDbc.filePath = filePath;
    outDbc.fileName = QFileInfo(filePath).fileName();
    outDbc.version = "ARXML";

    // AUTOSAR ARXML 结构（简化）：
    // <AUTOSAR>
    //   <AR-PACKAGES>
    //     <AR-PACKAGE>
    //       <SHORT-NAME>...</SHORT-NAME>
    //       <ELEMENTS>
    //         <CAN-CLUSTER>...
    //         <CAN-FRAME>...
    //         <I-SIGNAL>...
    //         <I-SIGNAL-I-PDU>...
    //       </ELEMENTS>
    //     </AR-PACKAGE>
    //   </AR-PACKAGES>
    // </AUTOSAR>

    // 遍历所有 AR-PACKAGE → ELEMENTS → 子元素
    // 收集所有元素的 SHORT-NAME → 节点 映射
    QHash<QString, pugi::xml_node> refMap;

    // 递归遍历收集引用映射
    std::function<void(pugi::xml_node)> collectRefs = [&](pugi::xml_node node) {
        for (pugi::xml_node child = node.first_child(); !child.empty();
             child = child.next_sibling()) {
            // 收集 SHORT-NAME → 节点 映射
            pugi::xml_node sn = child.child("SHORT-NAME");
            if (!sn.empty()) {
                QString name = sn.child_value();
                if (!name.isEmpty())
                    refMap.insert(name, child);
            }
            // 递归
            collectRefs(child);
        }
    };

    pugi::xml_node root = doc.child("AUTOSAR");
    if (root.empty()) {
        // 尝试直接取根节点
        root = doc;
    }
    collectRefs(root);

    // 解析 CAN-FRAME 元素
    for (auto it = refMap.constBegin(); it != refMap.constEnd(); ++it) {
        pugi::xml_node node = it.value();
        QString nodeName = node.name();

        if (nodeName == "CAN-FRAME" || nodeName.contains("FRAME")) {
            DbcMessage msg;
            parseCanFrame(&node, msg);

            // 解析关联的 PDU
            pugi::xml_node pduRef = node.child("PDUREFD");
            if (!pduRef.empty()) {
                QString refName = pduRef.child_value();
                auto pduIt = refMap.find(refName);
                if (pduIt != refMap.end()) {
                    parsePdu(&pduIt.value(), msg);
                }
            }

            if (msg.id != 0 || !msg.signalList.isEmpty())
                outDbc.messages.append(msg);
        }
    }

    // 解析 CAN-CLUSTER 元素
    for (auto it = refMap.constBegin(); it != refMap.constEnd(); ++it) {
        pugi::xml_node node = it.value();
        if (QString(node.name()).contains("CAN-CLUSTER")) {
            parseCanCluster(&node);
        }
    }

    return true;
}

void ArxmlImporter::parseISignal(const void *nodePtr, DbcSignal &outSig)
{
    auto *node = static_cast<const pugi::xml_node *>(nodePtr);

    // SHORT-NAME → 信号名
    pugi::xml_node sn = node->child("SHORT-NAME");
    if (!sn.empty())
        outSig.name = sn.child_value();

    // LENGTH → bitLength
    pugi::xml_node len = node->child("LENGTH");
    if (!len.empty())
        outSig.bitLength = len.text().as_int(1);

    // BIT-POSITION → startBit
    pugi::xml_node bp = node->child("BIT-POSITION");
    if (!bp.empty())
        outSig.startBit = bp.text().as_int(0);

    // DATA-TYPE → factor/offset/min/max
    pugi::xml_node dtRef = node->child("DATATYPE-REF");
    if (!dtRef.empty()) {
        // 简化处理：默认值
        outSig.factor = 1.0;
        outSig.offset = 0.0;
    }

    // 默认 Intel byte order
    outSig.littleEndian = true;
}

void ArxmlImporter::parsePdu(const void *nodePtr, DbcMessage &outMsg)
{
    auto *node = static_cast<const pugi::xml_node *>(nodePtr);

    // LENGTH → DLC
    pugi::xml_node len = node->child("LENGTH");
    if (!len.empty())
        outMsg.dlc = len.text().as_int(8);

    // SIGNAL-TO-PDU-MAPPINGS → 信号映射
    pugi::xml_node mappings = node->child("SIGNAL-TO-PDU-MAPPINGS");
    if (mappings.empty()) {
        // 尝试不同的元素名
        mappings = node->child("I-SIGNAL-TO-PDU-MAPPING");
    }

    for (pugi::xml_node mapping = mappings.first_child(); !mapping.empty();
         mapping = mapping.next_sibling()) {
        DbcSignal sig;

        // SIGNAL-REF → 引用 I-SIGNAL
        pugi::xml_node sigRef = mapping.child("SIGNAL-REF");
        if (!sigRef.empty()) {
            // 解析引用的信号定义（简化处理：直接取 SHORT-NAME）
            pugi::xml_node sn = sigRef.child("SHORT-NAME");
            if (!sn.empty())
                sig.name = sn.child_value();
        }

        // BIT-POSITION
        pugi::xml_node bp = mapping.child("BIT-POSITION");
        if (!bp.empty())
            sig.startBit = bp.text().as_int(0);

        // 默认值
        if (sig.bitLength == 0) sig.bitLength = 1;
        sig.factor = 1.0;
        sig.offset = 0.0;
        sig.littleEndian = true;

        outMsg.signalList.append(sig);
    }
}

void ArxmlImporter::parseCanFrame(const void *nodePtr, DbcMessage &outMsg)
{
    auto *node = static_cast<const pugi::xml_node *>(nodePtr);

    // SHORT-NAME → 帧名
    pugi::xml_node sn = node->child("SHORT-NAME");
    if (!sn.empty())
        outMsg.name = sn.child_value();

    // IDENTIFIER → CAN ID
    pugi::xml_node id = node->child("IDENTIFIER");
    if (!id.empty())
        outMsg.id = id.text().as_uint(0);

    // FRAME-LENGTH → DLC
    pugi::xml_node len = node->child("FRAME-LENGTH");
    if (!len.empty())
        outMsg.dlc = len.text().as_int(8);
}

void ArxmlImporter::parseCanCluster(const void *nodePtr)
{
    auto *node = static_cast<const pugi::xml_node *>(nodePtr);

    // CAN-CLUSTER-VEHICLE-NAME → channel
    // BAUDRATE → 波特率
    pugi::xml_node br = node->child("BAUDRATE");
    if (!br.empty()) {
        // 可以获取波特率，但当前 DbcFile 结构不需要
    }
}

const void *ArxmlImporter::resolveRef(const QString &ref)
{
    // 简化实现：实际需要通过 refMap 查找
    // 在 loadFromFile 中用局部 refMap 处理
    return nullptr;
}
