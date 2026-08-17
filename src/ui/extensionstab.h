#ifndef EXTENSIONSTAB_H
#define EXTENSIONSTAB_H

#include <QWidget>

class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QPushButton;
class PluginManager;

/**
 * @brief 扩展标签页 — 插件管理中心
 *
 * 表格展示所有已发现插件的状态与操作按钮；
 * 插件的浏览/详情/离线安装在侧边扩展面板（ExtensionsPanel）完成。
 */
class ExtensionsTab : public QWidget
{
    Q_OBJECT
public:
    explicit ExtensionsTab(QWidget *parent = nullptr);

    /// 刷新插件列表
    void refresh();

signals:
    /// 请求激活插件（双击或点击启动按钮）
    void pluginActivateRequested(const QString &name);
    /// 请求停用插件
    void pluginDeactivateRequested(const QString &name);
    /// 请求启用/禁用插件
    void pluginToggleRequested(const QString &name, bool enable);

private slots:
    void onRefreshClicked();
    void onItemDoubleClicked(int row, int col);
    void onInstallOpk();                       // 安装 .opk 插件包
    void onTableContextMenu(const QPoint &pos); // 右键菜单（卸载）

private:
    void setupUi();
    void populateTable();

    PluginManager *m_pm;

    // ---- 插件表格 ----
    QLabel *m_pluginCount;
    QTableWidget *m_table;
    QPushButton *m_refreshBtn;
};

#endif // EXTENSIONSTAB_H
