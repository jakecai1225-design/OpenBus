#ifndef SPLITEDITORAREA_H
#define SPLITEDITORAREA_H

#include <QWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QMainWindow>
#include <QPointer>

class DockDropOverlay;
class SplitEditorArea;
class QTabBar;

/**
 * @brief Floating window for a detached editor tab (multi-monitor friendly).
 *
 * Double-click the native title bar (or the in-window tab) to merge back.
 * Closing the window also merges the tab back (does not destroy content).
 * Drag the in-window tab onto the main editor to dock with drop zones.
 */
class DetachedTabWindow : public QMainWindow
{
    Q_OBJECT
public:
    DetachedTabWindow(QWidget *widget, const QString &label,
                      SplitEditorArea *editor, QWidget *parent = nullptr);

    QWidget *containedWidget() const { return m_widget; }
    QString label() const { return m_label; }

    /// Steal content and close without emitting reattachRequested.
    QWidget *takePage();

signals:
    void reattachRequested(QWidget *widget, const QString &label);

protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *m_widget = nullptr;
    QString m_label;
    QTabBar *m_tabBar = nullptr;
    QPointer<SplitEditorArea> m_editor;
    bool m_suppressReattach = false;
    bool m_dragArmed = false;
    QPoint m_pressGlobal;
};

/**
 * @brief Industrial / VS Code-style splittable editor area.
 *
 * Drag a tab over a pane to see Center / Left / Right / Top / Bottom zones.
 * Drop outside the main editor to float; drag a float tab back to dock.
 * Double-click a main tab to detach; double-click a float title to merge.
 */
class SplitEditorArea : public QWidget
{
    Q_OBJECT

public:
    enum class DockZone {
        None = 0,
        Center,
        Left,
        Right,
        Top,
        Bottom,
        Float
    };
    Q_ENUM(DockZone)

    explicit SplitEditorArea(QWidget *parent = nullptr);
    ~SplitEditorArea() override;

    int addTab(QWidget *widget, const QString &label);
    QWidget *currentWidget() const;
    QTabWidget *activeTabWidget() const;
    QList<QTabWidget *> allTabWidgets() const;

    bool isPinned(QWidget *w) const;

    void detachTab(QTabWidget *tabs, int index);
    void reattachTab(QWidget *widget, const QString &label);
    void closeTab(QTabWidget *tabs, int index);

    void beginDockDragFromTab(QTabWidget *tabs, int index);
    void beginDockDragFromFloat(DetachedTabWindow *win);
    void updateDockDrag(const QPoint &globalPos);
    void finishDockDrag(const QPoint &globalPos);
    void cancelDockDrag();
    bool isDockDragging() const { return m_drag.active; }

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void tabContextMenuRequested(int index, const QPoint &pos);
    void tabListChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onTabBarContextMenu(int index, const QPoint &pos);

private:
    struct DockDragState {
        bool active = false;
        QPointer<QTabWidget> sourceTabs;
        int sourceIndex = -1;
        QPointer<QWidget> widget;
        QString label;
        QPointer<DetachedTabWindow> sourceFloat;
        bool fromFloat = false;
    };

    QSplitter *m_rootSplitter = nullptr;
    QTabWidget *m_firstTabs = nullptr;
    QList<DetachedTabWindow *> m_detachedWindows;
    DockDropOverlay *m_overlay = nullptr;
    DockDragState m_drag;

    QTabWidget *createTabWidget();
    void installDragOutFilter(QTabWidget *tabs);
    void setupCloseButton(QTabWidget *tabs, int index);

    void splitTab(QTabWidget *source, int index, Qt::Orientation orient);
    void insertBeside(QTabWidget *target, QWidget *widget, const QString &label,
                      Qt::Orientation orient, bool before);
    void dropWidget(QTabWidget *target, DockZone zone,
                    QWidget *widget, const QString &label);
    void removeEmptySplits();
    QSplitter *parentSplitter(QWidget *w) const;

    QTabWidget *tabWidgetAtGlobal(const QPoint &globalPos) const;
    DockZone hitTestZone(QTabWidget *target, const QPoint &globalPos) const;
    void showOverlay(QTabWidget *target, DockZone zone);
    void hideOverlay();

    void togglePin(QTabWidget *tabs, int index);
    void closeOthers(QTabWidget *tabs, int keepIndex);
    void closeRight(QTabWidget *tabs, int startIndex);
    void closeAll(QTabWidget *tabs);

    void registerDetachedWindow(DetachedTabWindow *win);
    DetachedTabWindow *createDetachedWindow(QWidget *widget, const QString &label,
                                            const QPoint &globalPos);
};

#endif // SPLITEDITORAREA_H
