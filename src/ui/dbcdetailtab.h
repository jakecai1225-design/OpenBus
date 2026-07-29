#ifndef DBCDETAILTAB_H
#define DBCDETAILTAB_H

#include <QWidget>
#include <QHash>
#include "core/dbcdata.h"

class QMenu;

class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QLineEdit;
class QLabel;
class QStackedWidget;
class QSplitter;
class DbcManager;

/**
 * @brief DBC 详情标签页 — CANdb++ Editor 风格
 *
 * 左侧: 分层树 (Network → Nodes/Messages/ValueTables → Signals)
 * 右侧: 上下文敏感详情面板 (Message/Signal/Node/ValueTable)
 */
class DbcDetailTab : public QWidget
{
    Q_OBJECT

public:
    explicit DbcDetailTab(const QString &dbcFileName, DbcManager *mgr, QWidget *parent = nullptr);

signals:
    void signalDoubleClicked(quint32 canId, const QString &signalName);
    void signalAddToGraphic(quint32 canId, const QString &signalName);
    void signalAddToTrace(quint32 canId, const QString &signalName);

private slots:
    void onSearchChanged(const QString &text);
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onTreeContextMenu(const QPoint &pos);

private:
    // 树节点类型
    enum class NodeType {
        Network, CategoryNodes, CategoryMessages, CategoryValueTables,
        Node, Message, Signal, ValueTable
    };

    QString m_dbcFileName;
    DbcManager *m_dbcMgr;

    // 左侧
    QLineEdit *m_searchEdit;
    QTreeWidget *m_tree;

    // 右侧 — Stacked pages
    QStackedWidget *m_detailStack;
    int m_pageMessage = 0;
    int m_pageSignal = 1;
    int m_pageNode = 2;
    int m_pageValueTable = 3;
    int m_pagePlaceholder = 4;

    // Message 详情页
    QLabel *m_msgTitleLabel;
    QLabel *m_msgInfoLabel;
    QLabel *m_msgCommentLabel;
    QTableWidget *m_msgSigTable;

    // Signal 详情页
    QLabel *m_sigTitleLabel;
    QTableWidget *m_sigPropTable;   // 属性名-值 两列
    QLabel *m_sigCommentLabel;
    QTableWidget *m_sigValueTable;  // 值表

    // Node 详情页
    QLabel *m_nodeTitleLabel;
    QLabel *m_nodeCommentLabel;
    QTableWidget *m_nodeTxTable;    // 发送的报文
    QTableWidget *m_nodeRxTable;    // 接收的信号

    // ValueTable 详情页
    QLabel *m_vtTitleLabel;
    QTableWidget *m_vtTable;

    void buildLeftPane(QSplitter *splitter);
    void buildRightPane(QSplitter *splitter);
    void buildMessagePage(QWidget *page);
    void buildSignalPage(QWidget *page);
    void buildNodePage(QWidget *page);
    void buildValueTablePage(QWidget *page);

    void refreshTree();
    void showMessageDetail(quint32 canId);
    void showSignalDetail(quint32 canId, const QString &sigName);
    void showNodeDetail(const QString &nodeName);
    void showValueTableDetail(const QString &vtName);
    void showPlaceholder();

    const DbcFile *currentDbcFile() const;
};

#endif // DBCDETAILTAB_H
