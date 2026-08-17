#ifndef PLUGINDETAILPAGE_H
#define PLUGINDETAILPAGE_H

#include <QWidget>
#include <QPixmap>

class QLabel;
class QPushButton;
class QVBoxLayout;
class PluginManager;

namespace PluginUi {
/// 插件图标：优先加载 iconPath 指向的图片，失败时绘制按名称着色的首字母头像
QPixmap pluginIconPixmap(const QString &iconPath, const QString &name, int size);
}

/**
 * @brief 插件详情页 — VS Code 扩展详情布局
 *
 * 多个插件共用一个页面实例：showPlugin() 切换到指定插件，
 * 状态随 PluginManager::pluginListChanged 自动刷新。
 * 启动/停用/启用/禁用/卸载操作直接作用于 PluginManager。
 */
class PluginDetailPage : public QWidget
{
    Q_OBJECT
public:
    explicit PluginDetailPage(QWidget *parent = nullptr);

    /// 显示指定插件的详情
    void showPlugin(const QString &name);

    /// 当前展示的插件名
    QString currentPlugin() const { return m_name; }

private slots:
    void onPluginListChanged();

private:
    void buildUi();
    void refresh();
    void rebuildBody();

    PluginManager *m_pm;
    QString m_name;

    // ---- 头部（固定结构）----
    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_descLabel = nullptr;
    QPushButton *m_activateBtn = nullptr;    ///< 启动 / 停止
    QPushButton *m_enableBtn = nullptr;      ///< 启用 / 禁用
    QPushButton *m_uninstallBtn = nullptr;   ///< 卸载

    // ---- 正文（随插件动态重建）----
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
};

#endif // PLUGINDETAILPAGE_H
