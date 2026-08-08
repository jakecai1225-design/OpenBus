#include "arxml_exporter.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>

ArxmlExporter::ArxmlExporter()
{
}

bool ArxmlExporter::saveToFile(const QString &filePath, const DbcFile &dbc)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = "Cannot open file for writing: " + filePath;
        return false;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    // ARXML XML 声明 + 根元素
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<AUTOSAR xmlns=\"http://autosar.org/schema/R4.0\">\n";
    out << "  <AR-PACKAGES>\n";
    out << "    <AR-PACKAGE>\n";
    out << "      <SHORT-NAME>sin_export</SHORT-NAME>\n";
    out << "      <ELEMENTS>\n";

    // I-SIGNAL 定义
    for (const auto &msg : dbc.messages) {
        for (const auto &sig : msg.signalList) {
            out << "        <I-SIGNAL>\n";
            out << "          <SHORT-NAME>" << sig.name << "</SHORT-NAME>\n";
            out << "          <LENGTH>" << sig.bitLength << "</LENGTH>\n";
            if (sig.startBit > 0)
                out << "          <BIT-POSITION>" << sig.startBit
                    << "</BIT-POSITION>\n";
            out << "        </I-SIGNAL>\n";
        }
    }

    // I-SIGNAL-I-PDU 定义
    for (const auto &msg : dbc.messages) {
        out << "        <I-SIGNAL-I-PDU>\n";
        out << "          <SHORT-NAME>PDU_" << msg.name << "</SHORT-NAME>\n";
        out << "          <LENGTH>" << msg.dlc * 8 << "</LENGTH>\n";
        out << "          <SIGNAL-TO-PDU-MAPPINGS>\n";
        for (const auto &sig : msg.signalList) {
            out << "            <I-SIGNAL-TO-PDU-MAPPING>\n";
            out << "              <SIGNAL-REF DEST=\"I-SIGNAL\">"
                << sig.name << "</SIGNAL-REF>\n";
            if (sig.startBit > 0)
                out << "              <BIT-POSITION>"
                    << sig.startBit << "</BIT-POSITION>\n";
            out << "            </I-SIGNAL-TO-PDU-MAPPING>\n";
        }
        out << "          </SIGNAL-TO-PDU-MAPPINGS>\n";
        out << "        </I-SIGNAL-I-PDU>\n";
    }

    // CAN-FRAME 定义
    for (const auto &msg : dbc.messages) {
        out << "        <CAN-FRAME>\n";
        out << "          <SHORT-NAME>" << msg.name << "</SHORT-NAME>\n";
        out << "          <IDENTIFIER>" << msg.id << "</IDENTIFIER>\n";
        out << "          <FRAME-LENGTH>" << msg.dlc << "</FRAME-LENGTH>\n";
        out << "          <PDUREFD>PDU_" << msg.name << "</PDUREFD>\n";
        out << "        </CAN-FRAME>\n";
    }

    // CAN-CLUSTER 定义
    out << "        <CAN-CLUSTER>\n";
    out << "          <SHORT-NAME>sin_cluster</SHORT-NAME>\n";
    out << "          <BAUDRATE>500000</BAUDRATE>\n";
    out << "        </CAN-CLUSTER>\n";

    out << "      </ELEMENTS>\n";
    out << "    </AR-PACKAGE>\n";
    out << "  </AR-PACKAGES>\n";
    out << "</AUTOSAR>\n";

    file.close();
    return true;
}
