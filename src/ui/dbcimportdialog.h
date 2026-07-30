#ifndef DBCIMPORTDIALOG_H
#define DBCIMPORTDIALOG_H

#include <QDialog>
#include <QList>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class DbcManager;
class QRadioButton;

/**
 * @brief DBC 导入选择弹框
 *
 * 从已加载的 DBC 文件或从文件系统导入 CAN 报文到发送列表。
 * 支持按 CAN ID 或信号名搜索过滤。
 */
class DbcImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DbcImportDialog(DbcManager *dbcManager, QWidget *parent = nullptr);

    /// 返回用户选中的 CAN ID 列表
    QList<quint32> selectedCanIds() const;

private slots:
    void onSearchChanged(const QString &text);
    void onImportFromFile();

private:
    DbcManager *m_dbcManager;
    QTreeWidget *m_tree;
    QLineEdit *m_searchEdit;
    QRadioButton *m_searchByIdBtn;
    QRadioButton *m_searchBySignalBtn;
    QList<quint32> m_selectedIds;

    void populateTree();
    void filterTree(const QString &text, bool byId);
};

#endif // DBCIMPORTDIALOG_H
