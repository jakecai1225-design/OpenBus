#ifndef DBCSIGNALPICKERDIALOG_H
#define DBCSIGNALPICKERDIALOG_H

#include <QDialog>
#include <QList>
#include <QSet>
#include "core/dbcdata.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;
class DbcManager;

/**
 * @brief DBC 信号选择对话框（Graphic 侧栏「添加信号」入口）
 *
 * 数据源：DbcManager 已加载的全部 DBC 数据库。
 * 三级树：数据库文件（仅导航，不可选）→ 报文（选中 = 添加其全部信号）
 * → 信号。
 * 交互：
 *   - 顶部搜索框跨全部数据库按信号名/报文名过滤（防抖 250ms，
 *     命中分支自动展开；报文名命中时保留其全部信号）
 *   - 树形 Ctrl/Shift 多选（ExtendedSelection）
 *   - 双击报文/信号行 = 直接确认添加
 *   - 底部实时显示已选信号数；「添加」在零选择时禁用
 *   - 对话框打开期间数据库加载/卸载自动重灌
 */
class DbcSignalPickerDialog : public QDialog
{
    Q_OBJECT

public:
    /// 选中的信号（结果快照，accept 后经 pickedSignals() 读取）
    struct PickedSignal {
        QString fileName;      ///< 来源 DBC 文件名（输出提示用）
        QString messageName;   ///< 所属报文名
        quint32 canId = 0;
        bool extended = false;
        DbcSignal signal;      ///< 信号定义拷贝
    };

    /// title：窗口标题（缺省「添加信号到 Graphic」；Watcher 传「添加观测变量」）
    explicit DbcSignalPickerDialog(DbcManager *mgr, const QString &title = QString(),
                                   QWidget *parent = nullptr);

    /// 确认后取选中的信号列表（已按 canId+信号名去重）
    QList<PickedSignal> pickedSignals() const { return m_picked; }

public slots:
    void accept() override;

private slots:
    void scheduleRebuild();     ///< 搜索词变化 → 防抖重建
    void rebuildTree();         ///< 依据当前搜索词重建三级树
    void updateCountLabel();    ///< 已选信号数 → 标签 + 添加按钮可用态
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    DbcManager *m_mgr = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QTimer *m_debounce = nullptr;
    QList<PickedSignal> m_picked;

    int effectiveSignalCount() const;   ///< 报文行展开为其子信号数后的总数
    void collectSelection();            ///< 遍历选中行 → m_picked（去重 + 解引用）
    void pickSignalItem(QTreeWidgetItem *sigItem, QSet<QString> &seen);
};

#endif // DBCSIGNALPICKERDIALOG_H
