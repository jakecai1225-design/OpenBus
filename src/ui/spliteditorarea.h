#ifndef SPLITEDITORAREA_H
#define SPLITEDITORAREA_H

#include <QWidget>
#include <QSplitter>
#include <QTabWidget>

/**
 * @brief VS Code 风格可拆分编辑器区域
 *
 * 支持右键标签页 "Split Right" / "Split Down" 创建并排视图。
 * 每个拆分组是一个 QTabWidget，组与组之间用 QSplitter 分隔。
 * 当一个组没有标签页时自动移除该组。
 */
class SplitEditorArea : public QWidget
{
    Q_OBJECT

public:
    explicit SplitEditorArea(QWidget *parent = nullptr);

    int addTab(QWidget *widget, const QString &label);
    QWidget *currentWidget() const;
    QTabWidget *activeTabWidget() const;
    QList<QTabWidget *> allTabWidgets() const;

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void tabContextMenuRequested(int index, const QPoint &pos);

private slots:
    void onTabBarContextMenu(int index, const QPoint &pos);

private:
    QSplitter *m_rootSplitter = nullptr;
    QTabWidget *m_firstTabs = nullptr;

    QTabWidget *createTabWidget();
    void splitTab(QTabWidget *source, int index, Qt::Orientation orient);
    void removeEmptySplits();
    QSplitter *parentSplitter(QWidget *w) const;
    void replaceWidgetInSplitter(QSplitter *split, QWidget *old, QWidget *newW);
};

#endif // SPLITEDITORAREA_H
