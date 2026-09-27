#ifndef DBCSIGNALLISTVIEW_H
#define DBCSIGNALLISTVIEW_H

#include <QWidget>

class DbcManager;
class QTableWidget;
class QComboBox;
class QLabel;

/**
 * @brief DBC 信号清单导出工具（DBC 模块 "signallist" 页）
 *
 * 列出已加载 DBC 的全部报文/信号，支持筛选与导出 CSV。
 */
class DbcSignalListView : public QWidget
{
    Q_OBJECT

public:
    explicit DbcSignalListView(DbcManager *dbc, QWidget *parent = nullptr);

public slots:
    void refresh();

private slots:
    void onExportCsv();
    void onFilterChanged(const QString &text);
    void onFileFilterChanged(int index);

private:
    void rebuildTable();

    DbcManager *m_dbc = nullptr;
    QTableWidget *m_table = nullptr;
    QComboBox *m_fileCombo = nullptr;
    class QLineEdit *m_filterEdit = nullptr;
    QLabel *m_countLabel = nullptr;
};

#endif // DBCSIGNALLISTVIEW_H
