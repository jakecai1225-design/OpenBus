#ifndef DBCDETAILTAB_H
#define DBCDETAILTAB_H

#include <QWidget>
#include "core/dbcdata.h"

class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QLineEdit;
class QLabel;
class DbcManager;

/**
 * @brief DBC 详情标签页 — 中央区域
 *
 * 左侧: 树形结构 (网络 → 节点 → 报文 → 信号) + 搜索栏
 * 右侧: 选中 Message 的详情 + Signal 表格
 */
class DbcDetailTab : public QWidget
{
    Q_OBJECT

public:
    explicit DbcDetailTab(const QString &dbcFileName, DbcManager *mgr, QWidget *parent = nullptr);

signals:
    void signalDoubleClicked(quint32 canId, const QString &signalName);

private slots:
    void onSearchChanged(const QString &text);
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void onTreeItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QString m_dbcFileName;
    DbcManager *m_dbcMgr;

    QLineEdit *m_searchEdit;
    QTreeWidget *m_tree;
    QLabel *m_msgTitleLabel;
    QLabel *m_msgInfoLabel;
    QTableWidget *m_sigTable;

    void refreshTree();
    void showMessageDetail(quint32 canId);
};

#endif // DBCDETAILTAB_H
