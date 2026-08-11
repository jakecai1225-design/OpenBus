#ifndef COLUMNFILTERPOPUP_H
#define COLUMNFILTERPOPUP_H

#include <QFrame>
#include <QHash>
#include <QStringList>
#include <QSet>

class QCheckBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

/**
 * @brief Excel 风格列筛选弹出面板
 *
 * 点击表头漏斗图标后弹出，包含：
 * - 搜索框：在值列表中搜索
 * - 全选/清除/反选按钮
 * - 复选框列表：每行显示值文本和计数
 * - 确定/取消按钮
 *
 * 面板关闭时通过 selectedValues() 返回选中的值集合
 */
class ColumnFilterPopup : public QFrame
{
    Q_OBJECT

public:
    struct ValueItem {
        QString text;   ///< 显示文本
        int count = 0;  ///< 出现次数
    };

    explicit ColumnFilterPopup(int column, QWidget *parent = nullptr);

    /// 设置所有可选值
    void setValues(const QList<ValueItem> &values);

    /// 设置当前选中的值（用于恢复状态）
    void setSelectedValues(const QSet<QString> &selected);

    /// 获取当前选中的值集合
    QSet<QString> selectedValues() const;

    /// 是否处于"全选"状态（无过滤）
    bool isAllSelected() const;

    int column() const { return m_column; }

signals:
    /// 用户点击确定时发出，传递选中的值集合
    void filterApplied(int column, const QSet<QString> &selected);
    /// 用户请求清除该列筛选
    void filterCleared(int column);

private slots:
    void onSearchChanged(const QString &text);
    void onSelectAll();
    void onClearAll();
    void onInvert();
    void onItemChanged(QListWidgetItem *item);
    void onOk();
    void onClearFilter();

private:
    int m_column;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_selectAllBtn = nullptr;
    QPushButton *m_clearAllBtn = nullptr;
    QPushButton *m_invertBtn = nullptr;

    /// 原始值列表（不受搜索框影响）
    QList<ValueItem> m_allValues;
    /// 当前选中值集合
    QSet<QString> m_selectedValues;

    void buildList(const QString &filter = {});
    void updateSelectAllState();
};

#endif // COLUMNFILTERPOPUP_H
