#include "rightpanel.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPlainTextEdit>
#include <QHeaderView>
#include <QVBoxLayout>

RightPanel::RightPanel(QWidget *parent)
    : QTabWidget(parent)
{
    setObjectName("RightPanel");

    // ---- Properties 标签页 ----
    m_propTable = new QTableWidget(7, 2, this);
    m_propTable->setHorizontalHeaderLabels({"属性", "值"});
    m_propTable->verticalHeader()->setVisible(false);
    m_propTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_propTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_propTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_propTable->setItem(0, 0, new QTableWidgetItem("Time"));
    m_propTable->setItem(1, 0, new QTableWidgetItem("Channel"));
    m_propTable->setItem(2, 0, new QTableWidgetItem("Direction"));
    m_propTable->setItem(3, 0, new QTableWidgetItem("ID"));
    m_propTable->setItem(4, 0, new QTableWidgetItem("DLC"));
    m_propTable->setItem(5, 0, new QTableWidgetItem("Data"));
    m_propTable->setItem(6, 0, new QTableWidgetItem("Flags"));
    for (int i = 0; i < 7; ++i)
        m_propTable->setItem(i, 1, new QTableWidgetItem("-"));
    addTab(m_propTable, "Properties");

    // ---- Statistics 标签页 ----
    m_statTable = new QTableWidget(6, 2, this);
    m_statTable->setHorizontalHeaderLabels({"统计项", "值"});
    m_statTable->verticalHeader()->setVisible(false);
    m_statTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_statTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_statTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_statTable->setItem(0, 0, new QTableWidgetItem("总帧数"));
    m_statTable->setItem(1, 0, new QTableWidgetItem("Rx 帧"));
    m_statTable->setItem(2, 0, new QTableWidgetItem("Tx 帧"));
    m_statTable->setItem(3, 0, new QTableWidgetItem("CAN FD 帧"));
    m_statTable->setItem(4, 0, new QTableWidgetItem("扩展帧"));
    m_statTable->setItem(5, 0, new QTableWidgetItem("总线负载"));
    for (int i = 0; i < 6; ++i)
        m_statTable->setItem(i, 1, new QTableWidgetItem("0"));
    addTab(m_statTable, "Statistics");

    // ---- Bookmarks 标签页 ----
    m_bookmarks = new QPlainTextEdit(this);
    m_bookmarks->setReadOnly(true);
    m_bookmarks->setPlaceholderText("双击 Trace 行可添加书签...");
    addTab(m_bookmarks, "Bookmarks");

    setMinimumWidth(200);
    setMaximumWidth(400);
}

void RightPanel::setFrameProperties(const QString &time, const QString &channel,
                                    const QString &direction, const QString &id,
                                    const QString &dlc, const QString &data,
                                    const QString &flags)
{
    m_propTable->setItem(0, 1, new QTableWidgetItem(time));
    m_propTable->setItem(1, 1, new QTableWidgetItem(channel));
    m_propTable->setItem(2, 1, new QTableWidgetItem(direction));
    m_propTable->setItem(3, 1, new QTableWidgetItem(id));
    m_propTable->setItem(4, 1, new QTableWidgetItem(dlc));
    m_propTable->setItem(5, 1, new QTableWidgetItem(data));
    m_propTable->setItem(6, 1, new QTableWidgetItem(flags));
}

void RightPanel::updateStatistics(int totalFrames, int rxCount, int txCount,
                                  int canFdCount, int extCount, double busLoad)
{
    m_statTable->setItem(0, 1, new QTableWidgetItem(QString::number(totalFrames)));
    m_statTable->setItem(1, 1, new QTableWidgetItem(QString::number(rxCount)));
    m_statTable->setItem(2, 1, new QTableWidgetItem(QString::number(txCount)));
    m_statTable->setItem(3, 1, new QTableWidgetItem(QString::number(canFdCount)));
    m_statTable->setItem(4, 1, new QTableWidgetItem(QString::number(extCount)));
    m_statTable->setItem(5, 1, new QTableWidgetItem(QString::number(busLoad, 'f', 2) + "%"));
}

void RightPanel::clearAll()
{
    for (int i = 0; i < 7; ++i)
        m_propTable->setItem(i, 1, new QTableWidgetItem("-"));
    for (int i = 0; i < 6; ++i)
        m_statTable->setItem(i, 1, new QTableWidgetItem("0"));
    m_bookmarks->clear();
}
