#ifndef SHORTCUTSPAGE_H
#define SHORTCUTSPAGE_H

#include <QWidget>

/**
 * @brief 快捷键参考页（标签页形态）
 *
 * 侧栏设置面板"快捷键"条目以标签页打开（不再弹窗）；
 * 帮助菜单的快捷键对话框与本页共用 shortcutsText() 文案。
 */
class ShortcutsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutsPage(QWidget *parent = nullptr);

    /// 快捷键参考文案（帮助菜单对话框与标签页共用）
    static QString shortcutsText();
};

#endif // SHORTCUTSPAGE_H
