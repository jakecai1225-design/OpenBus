#ifndef SPLITEDITORAREA_H
#define SPLITEDITORAREA_H

#include <QWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QMainWindow>

/**
 * @brief 分离标签页的独立窗口
 *
 * 当标签页被拖出主窗口或通过右键菜单分离时，widget 会被放入此窗口。
 * 关闭此窗口时，widget 会被重新放回 SplitEditorArea 的标签页中。
 */
class DetachedTabWindow : public QMainWindow
{
    Q_OBJECT
public:
    DetachedTabWindow(QWidget *widget, const QString &label, QWidget *parent = nullptr);

    QWidget *containedWidget() const { return m_widget; }
    QString label() const { return m_label; }

signals:
    /// 窗口关闭时请求重新放回主标签页
    void reattachRequested(QWidget *widget, const QString &label);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *m_widget = nullptr;
    QString m_label;
};

// ============================================================

/**
 * @brief VS Code 风格可拆分编辑器区域
 *
 * 支持右键标签页 "Split Right" / "Split Down" 创建并排视图。
 * 每个拆分组是一个 QTabWidget，组与组之间用 QSplitter 分隔。
 * 当一个组没有标签页时自动移除该组。
 * 支持拖拽标签页到主窗口外分离为独立窗口。
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

    /// 将标签页分离到独立窗口
    void detachTab(QTabWidget *tabs, int index);

    /// 将独立窗口中的 widget 重新放回标签页
    void reattachTab(QWidget *widget, const QString &label);

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void tabContextMenuRequested(int index, const QPoint &pos);
    void tabListChanged();

private slots:
    void onTabBarContextMenu(int index, const QPoint &pos);

private:
    QSplitter *m_rootSplitter = nullptr;
    QTabWidget *m_firstTabs = nullptr;
    QList<DetachedTabWindow *> m_detachedWindows;

    QTabWidget *createTabWidget();
    void splitTab(QTabWidget *source, int index, Qt::Orientation orient);
    void removeEmptySplits();
    QSplitter *parentSplitter(QWidget *w) const;
    void replaceWidgetInSplitter(QSplitter *split, QWidget *old, QWidget *newW);

    // 标签页拖拽分离检测
    void installDragOutFilter(QTabWidget *tabs);
};

#endif // SPLITEDITORAREA_H
